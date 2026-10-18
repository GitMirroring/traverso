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


TPeakVerifyTask::TPeakVerifyTask(QString peakPath, TPeakFile::SourceInfo expected, qint64 sliceBytes)
    : m_peakPath(std::move(peakPath))
    , m_expected(expected)
    , m_sliceBytes(std::max<qint64>(4096, sliceBytes & ~qint64(3)))
{
}

bool TPeakVerifyTask::step(std::chrono::nanoseconds budget)
{
    if (m_state != State::Waiting && m_state != State::Running) {
        return false;
    }
    if (m_cancelled.load(std::memory_order_relaxed)) {
        m_state = State::Cancelled;
        m_file.close();
        return false;
    }
    const auto started = std::chrono::steady_clock::now();

    if (m_state == State::Waiting) {
        if (m_file.open(m_peakPath, m_expected) != TPeakFile::Status::Ok) {
            m_error = QStringLiteral("the peak file does not open");
            m_state = State::Failed;
            return false;
        }
        m_checksum = TPeakFile::checksum_begin();
        m_state = State::Running;
    }

    do {
        const qint64 bytes = std::min(m_sliceBytes, m_file.data_size() - m_position);
        if (bytes <= 0) {
            const bool whole = m_file.checksum_matches(m_checksum);
            m_file.close();
            if (!whole) {
                m_error = QStringLiteral("the peak file does not match its checksum");
                m_state = State::Failed;
                TLOG_W("peaks", "{} does not match its checksum", m_peakPath.toStdString());
                return false;
            }
            m_progress = 100;
            m_state = State::Done;
            return false;
        }
        m_checksum = m_file.checksum_add(m_checksum, m_position, bytes);
        m_position += bytes;
        m_progress = int(m_position * 100 / std::max<qint64>(1, m_file.data_size()));
    } while (std::chrono::steady_clock::now() - started < budget);
    return true;
}
