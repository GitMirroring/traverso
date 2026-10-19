// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TPeakBuildTask.h"

#include "ResampleAudioReader.h"
#include "TFileIOBuffer.h"
#include "TLog.h"

#include <QFile>

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

TPeakBuildTask::TPeakBuildTask(OpenSource openSource, QString audioPath, std::shared_ptr<TPeakStream> stream, qint64 readFrames)
    : m_openSource(std::move(openSource))
    , m_audioPath(std::move(audioPath))
    , m_stream(std::move(stream))
    , m_readFrames(readFrames)
{
    Q_ASSERT(m_stream);
}

TPeakBuildTask::~TPeakBuildTask() = default;

bool TPeakBuildTask::fail(const QString& error, State state)
{
    m_error = error;
    m_state = state;
    if (state == State::Unsupported) {
        TLOG_I("peaks", "{} is not built: {}", peak_path().toStdString(), error.toStdString());
    } else {
        TLOG_W("peaks", "building {} failed: {}", peak_path().toStdString(), error.toStdString());
    }
    // What is left of an unfinished peak file goes
    m_stream->abandon();
    release();
    return false;
}

void TPeakBuildTask::release()
{
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
    if (!supports(channels)) {
        return fail(QStringLiteral("sources with %1 channels are not supported").arg(channels), State::Unsupported);
    }
    if (rate != m_stream->rate() || channels != m_stream->channel_count()) {
        return fail(QStringLiteral("the source has %1 Hz and %2 channels, its waveform expects %3 Hz and %4")
                        .arg(rate).arg(channels).arg(m_stream->rate()).arg(m_stream->channel_count()));
    }
    if (m_stream->open(&error) != TPeakFile::Status::Ok) {
        return fail(error);
    }

    // A fifth of a second: what a reader reads at least per refill
    if (m_readFrames <= 0) {
        m_readFrames = qint64(rate) / 5;
    }
    m_readFrames = std::max<qint64>(TPeakLevels::BlockFrames, m_readFrames);
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
    QString error;
    if (m_stream->finish(m_audioPath, &error) != TPeakFile::Status::Ok) {
        return fail(error);
    }
    m_progress = 100;
    m_state = State::Written;
    release();   // the finished part file stays
    return false;
}

TPeakBuildTask::State TPeakBuildTask::publish()
{
    if (m_state != State::Written) {
        return m_state;
    }
    if (m_cancelled.load(std::memory_order_relaxed)) {
        m_stream->release_mapping();
        QFile::remove(TPeakFileWriter::part_path(peak_path()));
        m_state = State::Cancelled;
        return m_state;
    }
    QString error;
    if (m_stream->publish(&error) != TPeakFile::Status::Ok) {
        m_error = error;
        m_state = State::Failed;
        TLOG_W("peaks", "publishing {} failed: {}", peak_path().toStdString(), error.toStdString());
        return m_state;
    }
    m_state = State::Done;
    TLOG_I("peaks", "built {}", peak_path().toStdString());
    return m_state;
}

bool TPeakBuildTask::step()
{
    if (m_state != State::Waiting && m_state != State::Running) {
        return false;
    }
    if (m_cancelled.load(std::memory_order_relaxed)) {
        m_state = State::Cancelled;
        TLOG_D("peaks", "build of {} cancelled", peak_path().toStdString());
        m_stream->abandon();
        release();
        return false;
    }
    if (m_state == State::Waiting && !start()) {
        return false;
    }

    const qint64 toRead = std::min(m_readFrames, m_reportedFrames - m_position);
    const qint64 read = m_source->read(m_position, toRead, m_channels.data());
    if (read < 0 || (read == 0 && m_position == 0)) {
        return fail(QStringLiteral("reading the source failed at frame %1").arg(m_position));
    }
    if (read == 0) {
        // The source reported more frames than it delivers (MP3: one
        // frame short is common); the pyramid ends here
        TLOG_I("peaks", "{}: the source ends at frame {}, {} frames before the length it reports",
               peak_path().toStdString(), m_position, m_reportedFrames - m_position);
        return finish();
    }

    // Complete periods go to the part file at once, and out of memory once
    // no reader holds them (TPeakStream)
    m_stream->append(m_constChannels.data(), std::min(read, toRead));
    if (m_stream->write_failed()) {
        return fail(QStringLiteral("writing the peak file failed"));
    }
    m_position += std::min(read, toRead);
    m_progress = int(m_position * 100 / m_reportedFrames);
    if (m_position >= m_reportedFrames) {
        return finish();
    }
    return true;
}
