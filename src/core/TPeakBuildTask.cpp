// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TPeakBuildTask.h"

#include "ResampleAudioReader.h"
#include "TFileIOBuffer.h"
#include "TLog.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>

// An audio file read with Traverso's decoders at its own rate
class TAudioFileBuildSource final : public TPeakBuildSource
{
public:
    explicit TAudioFileBuildSource(const QString& fileName)
        : m_reader(fileName)
    {
    }

    bool is_valid() { return m_reader.is_valid(); }

    uint rate() const override { return m_reader.get_file_rate(); }
    int channel_count() const override { return int(m_reader.get_num_channels()); }
    qint64 frame_count() const override { return qint64(m_reader.get_nframes()); }

    qint64 read(qint64 firstFrame, qint64 count, float* const* channels) override
    {
        const int channelCount = channel_count();
        m_buffer.check_capacity(nframes_t(count), uint(channelCount));
        const qint64 read = qint64(m_reader.read_from(m_buffer, nframes_t(firstFrame), nframes_t(count)));
        const qint64 frames = std::min(read, count);
        for (int c = 0; c < channelCount; ++c) {
            std::copy_n(m_buffer.get_channel_buffer(uint(c)).get_data(nframes_t(frames)), frames, channels[c]);
        }
        return frames;
    }

private:
    mutable TResampleAudioReader m_reader;
    TFileIOBuffer m_buffer;
};


TPeakBuildTask::OpenSource TPeakBuildTask::audio_file(const QString& fileName)
{
    return [fileName](QString* error) -> std::unique_ptr<TPeakBuildSource> {
        auto source = std::make_unique<TAudioFileBuildSource>(fileName);
        if (!source->is_valid()) {
            *error = QStringLiteral("cannot read %1").arg(fileName);
            return nullptr;
        }
        return source;
    };
}

TPeakBuildTask::TPeakBuildTask(OpenSource openSource, QString audioPath, QString peakPath, int periodShift, qint64 readFrames)
    : m_openSource(std::move(openSource))
    , m_audioPath(std::move(audioPath))
    , m_peakPath(std::move(peakPath))
    , m_periodShift(periodShift)
    , m_readFrames(std::max<qint64>(TPeakLevels::BlockFrames, readFrames))
{
}

TPeakBuildTask::~TPeakBuildTask() = default;

bool TPeakBuildTask::fail(const QString& error)
{
    m_error = error;
    m_state = State::Failed;
    TLOG_W("peaks", "building {} failed: {}", m_peakPath.toStdString(), error.toStdString());
    release();
    return false;
}

void TPeakBuildTask::release()
{
    // What is left of an unfinished peak file says unfinished in its header
    m_writer.close();
    m_builder.reset();
    m_source.reset();
    m_samples.clear();
    m_channels.clear();
    m_constChannels.clear();
}

bool TPeakBuildTask::start()
{
    QString error;
    m_source = m_openSource(&error);
    if (!m_source) {
        return fail(error.isEmpty() ? QStringLiteral("cannot open the source") : error);
    }

    const uint rate = m_source->rate();
    const int channels = m_source->channel_count();
    m_reportedFrames = m_source->frame_count();
    if (rate == 0 || channels <= 0 || m_reportedFrames <= 0) {
        return fail(QStringLiteral("the source has no audio"));
    }
    if (channels > MaxChannels) {
        return fail(QStringLiteral("sources with %1 channels are not supported").arg(channels));
    }
    if (!QDir().mkpath(QFileInfo(m_peakPath).absolutePath())) {
        return fail(QStringLiteral("cannot create the peak file directory"));
    }
    if (m_writer.open(m_peakPath, rate, channels, m_periodShift, &error) != TPeakFile::Status::Ok) {
        return fail(error);
    }

    m_builder = std::make_unique<TPeakPyramidBuilder>(rate, channels, m_periodShift);
    m_samples.assign(size_t(channels), std::vector<float>(size_t(m_readFrames)));
    for (std::vector<float>& samples : m_samples) {
        m_channels.push_back(samples.data());
        m_constChannels.push_back(samples.data());
    }
    m_state = State::Running;
    return true;
}

bool TPeakBuildTask::finish()
{
    m_builder->finish();
    const TPeakFile::SourceInfo source = TPeakFile::SourceInfo::from_file(
        m_audioPath, m_builder->rate(), m_builder->channel_count(), m_builder->frame_count());
    QString error;
    if (m_writer.finish(m_builder->levels(), source, &error) != TPeakFile::Status::Ok) {
        return fail(error);
    }
    m_progress = 100;
    m_state = State::Done;
    TLOG_I("peaks", "built {}", m_peakPath.toStdString());
    release();
    return false;
}

bool TPeakBuildTask::step(std::chrono::nanoseconds budget)
{
    if (m_state != State::Waiting && m_state != State::Running) {
        return false;
    }
    const auto started = std::chrono::steady_clock::now();

    do {
        if (m_cancelled.load(std::memory_order_relaxed)) {
            m_state = State::Cancelled;
            TLOG_D("peaks", "build of {} cancelled", m_peakPath.toStdString());
            release();
            return false;
        }
        if (m_state == State::Waiting && !start()) {
            return false;
        }

        const qint64 toRead = std::min(m_readFrames, m_reportedFrames - m_position);
        if (toRead <= 0) {
            return finish();
        }
        const qint64 read = m_source->read(m_position, toRead, m_channels.data());
        if (read < 0 || (read == 0 && m_position == 0)) {
            return fail(QStringLiteral("reading the source failed at frame %1").arg(m_position));
        }
        if (read == 0) {
            // The source reported more frames than it delivers (MP3: one
            // frame short is common); the pyramid ends here
            TLOG_I("peaks", "{}: the source ends at frame {}, {} frames before the length it reports",
                   m_peakPath.toStdString(), m_position, m_reportedFrames - m_position);
            return finish();
        }

        m_builder->append(m_constChannels.data(), std::min(read, toRead));
        m_position += std::min(read, toRead);

        // A complete period goes to the file, and out of memory
        const qint64 periods = m_builder->complete_periods();
        if (periods > m_writer.written_periods()) {
            QString error;
            if (m_writer.write_periods(m_builder->levels(), periods, &error) != TPeakFile::Status::Ok) {
                return fail(error);
            }
            m_builder->release_periods(periods);
        }
        m_progress = int(m_position * 100 / m_reportedFrames);
    } while (std::chrono::steady_clock::now() - started < budget);

    return true;
}


TPeakWriteTask::TPeakWriteTask(std::shared_ptr<const TPeakPyramidBuilder> pyramid, QString audioPath, QString peakPath)
    : m_pyramid(std::move(pyramid))
    , m_audioPath(std::move(audioPath))
    , m_peakPath(std::move(peakPath))
{
    Q_ASSERT(m_pyramid && m_pyramid->is_finished());
}

bool TPeakWriteTask::fail(const QString& error)
{
    m_error = error;
    m_state = State::Failed;
    TLOG_W("peaks", "writing {} failed: {}", m_peakPath.toStdString(), error.toStdString());
    m_writer.close();
    m_pyramid.reset();
    return false;
}

bool TPeakWriteTask::step(std::chrono::nanoseconds budget)
{
    if (m_state != State::Waiting && m_state != State::Running) {
        return false;
    }
    if (m_cancelled.load(std::memory_order_relaxed)) {
        m_state = State::Cancelled;
        m_writer.close();
        m_pyramid.reset();
        return false;
    }
    const auto started = std::chrono::steady_clock::now();

    if (m_state == State::Waiting) {
        QString error;
        if (!QDir().mkpath(QFileInfo(m_peakPath).absolutePath())) {
            return fail(QStringLiteral("cannot create the peak file directory"));
        }
        if (m_writer.open(m_peakPath, m_pyramid->rate(), m_pyramid->channel_count(), m_pyramid->period_shift(), &error)
                != TPeakFile::Status::Ok) {
            return fail(error);
        }
        m_periods = m_pyramid->complete_periods();
        m_state = State::Running;
    }

    // One record per round, then the tail and the header in the last one
    const TPeakLevels levels = m_pyramid->levels();
    QString error;
    while (m_writer.written_periods() < m_periods) {
        if (m_writer.write_periods(levels, m_writer.written_periods() + 1, &error) != TPeakFile::Status::Ok) {
            return fail(error);
        }
        m_progress = int(m_writer.written_periods() * 100 / (m_periods + 1));
        if (std::chrono::steady_clock::now() - started >= budget) {
            return true;
        }
    }

    const TPeakFile::SourceInfo source = TPeakFile::SourceInfo::from_file(
        m_audioPath, m_pyramid->rate(), m_pyramid->channel_count(), m_pyramid->frame_count());
    if (m_writer.finish(levels, source, &error) != TPeakFile::Status::Ok) {
        return fail(error);
    }
    m_progress = 100;
    m_state = State::Done;
    TLOG_I("peaks", "wrote {}", m_peakPath.toStdString());
    m_pyramid.reset();   // the memory goes as soon as the file is there
    return false;
}
