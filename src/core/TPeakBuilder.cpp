// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TPeakBuilder.h"

#include "TBufferedAudioStreamReader.h"
#include "TFileIOBuffer.h"
#include "TLog.h"
#include "TPeakFile.h"
#include "TPeakPyramid.h"
#include "TProjectManager.h"
#include "TResourcesManager.h"

#include <QDir>
#include <QFileInfo>
#include <QMutexLocker>

#include <vector>

static constexpr nframes_t ReadChunk = 65536;

TPeakBuildJob::TPeakBuildJob(TBufferedAudioStreamReader* source, const QString& peakPath)
    : m_source(source)
    , m_peakPath(peakPath)
{
}

TPeakBuildJob::TPeakBuildJob(std::shared_ptr<TPeakPyramidBuilder> pyramid, const QString& audioPath, const QString& peakPath)
    : m_pyramid(std::move(pyramid))
    , m_audioPath(audioPath)
    , m_peakPath(peakPath)
{
}

TPeakBuildJob::~TPeakBuildJob()
{
    delete m_source;
}

void TPeakBuildJob::run()
{
    if (m_pyramid) {
        write();
    } else {
        build();
    }

    if (m_cancelled.load()) {
        TLOG_D("peaks", "build of {} cancelled", m_peakPath.toStdString());
    } else if (m_succeeded) {
        TLOG_I("peaks", "{} {}", m_pyramid ? "wrote" : "built", m_peakPath.toStdString());
    } else {
        TLOG_W("peaks", "{} {} failed: {}", m_pyramid ? "writing" : "building", m_peakPath.toStdString(), m_error.toStdString());
    }

    m_done.store(true);
}

void TPeakBuildJob::write()
{
    if (!QDir().mkpath(QFileInfo(m_peakPath).absolutePath())) {
        m_error = QStringLiteral("cannot create the peak file directory");
        return;
    }
    // The audio file is complete by now: its size and time go into the header
    const TPeakFile::SourceInfo source = TPeakFile::SourceInfo::from_file(
        m_audioPath, m_pyramid->rate(), m_pyramid->channel_count(), m_pyramid->frame_count());
    m_succeeded = TPeakFile::write(m_peakPath, m_pyramid->levels(), source, &m_error) == TPeakFile::Status::Ok;
    m_pyramid.reset();   // the memory goes as soon as the file is there
}

void TPeakBuildJob::build()
{
    const uint rate = m_source->get_file_rate();
    const uint channels = m_source->get_channel_count();
    const qint64 frames = m_source->get_nframes();

    if (rate == 0 || channels == 0 || frames <= 0) {
        m_error = QStringLiteral("the source has no audio");
    } else if (!QDir().mkpath(QFileInfo(m_peakPath).absolutePath())) {
        m_error = QStringLiteral("cannot create the peak file directory");
    } else {
        TPeakPyramidBuilder builder(rate, int(channels));
        TFileIOBuffer buffer;
        buffer.check_capacity(ReadChunk, channels);
        std::vector<const float*> pointers(channels);

        int lastPercent = -1;
        qint64 position = 0;
        while (position < frames && !m_cancelled.load()) {
            const nframes_t toRead = nframes_t(std::min<qint64>(ReadChunk, frames - position));
            const int read = m_source->file_read(buffer, nframes_t(position), toRead);
            if (read <= 0) {
                m_error = QStringLiteral("reading the source failed at frame %1").arg(position);
                break;
            }
            for (uint c = 0; c < channels; ++c) {
                pointers[c] = buffer.get_channel_buffer(c).get_data(nframes_t(read));
            }
            builder.append(pointers.data(), read);
            position += read;

            const int percent = int(position * 100 / frames);
            if (percent != lastPercent) {
                lastPercent = percent;
                emit progress(percent);
            }
        }

        if (m_error.isEmpty() && !m_cancelled.load()) {
            // A short read at the end (a decoder that reports more frames
            // than it delivers) leaves a shorter pyramid; the file then
            // records the frames that are really there
            builder.finish();
            const TPeakFile::SourceInfo source = TPeakFile::SourceInfo::from_file(
                m_source->get_filename(), rate, int(channels), builder.frame_count());
            m_succeeded = TPeakFile::write(m_peakPath, builder.levels(), source, &m_error) == TPeakFile::Status::Ok;
        }
    }

}


TPeakBuilder& peak_builder()
{
    static TPeakBuilder builder;
    return builder;
}

TPeakBuilder::TPeakBuilder()
    : m_worker(new QObject)
{
    m_worker->moveToThread(&m_thread);
    m_thread.setObjectName(QStringLiteral("peaks"));
    m_thread.start(QThread::LowPriority);
}

TPeakBuilder::~TPeakBuilder()
{
    {
        QMutexLocker locker(&m_mutex);
        for (const std::weak_ptr<TPeakBuildJob>& weak : m_jobs) {
            if (std::shared_ptr<TPeakBuildJob> job = weak.lock()) {
                job->cancel();
            }
        }
    }
    m_thread.quit();
    m_thread.wait();
    delete m_worker;
}

std::shared_ptr<TPeakBuildJob> TPeakBuilder::build(const QString& peakPath, qint64 sourceId)
{
    QMutexLocker locker(&m_mutex);

    if (std::shared_ptr<TPeakBuildJob> running = m_jobs.value(peakPath).lock()) {
        if (!running->is_done()) {
            return running;
        }
    }

    TBufferedAudioStreamReader* source = resources_manager()->get_readsource(sourceId);
    if (!source) {
        return nullptr;
    }
    return queue(peakPath, new TPeakBuildJob(source, peakPath));
}

std::shared_ptr<TPeakBuildJob> TPeakBuilder::write(const QString& peakPath, std::shared_ptr<TPeakPyramidBuilder> pyramid, const QString& audioPath)
{
    QMutexLocker locker(&m_mutex);
    return queue(peakPath, new TPeakBuildJob(std::move(pyramid), audioPath, peakPath));
}

std::shared_ptr<TPeakBuildJob> TPeakBuilder::queue(const QString& peakPath, TPeakBuildJob* newJob)
{
    // The job is a QObject of the calling thread; whichever thread drops
    // the last reference hands the deletion to that thread
    std::shared_ptr<TPeakBuildJob> job(newJob, [](TPeakBuildJob* j) { j->deleteLater(); });
    m_jobs.insert(peakPath, job);

    QMetaObject::invokeMethod(m_worker, [job] {
        TLog::name_thread("peaks");
        job->run();
        emit job->finished();
    }, Qt::QueuedConnection);

    return job;
}
