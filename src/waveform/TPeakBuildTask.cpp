// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TPeakBuildTask.h"

#include "TAudioReader.h"
#include "TFileIOBuffer.h"
#include "TLog.h"


#include <algorithm>

// An audio file read at its own rate
class TAudioFileBuildSource final : public TPeakBuildSource
{
public:
    explicit TAudioFileBuildSource(std::unique_ptr<TAudioReader> reader)
        : m_reader(std::move(reader))
    {
    }

    uint rate() const override { return m_reader->file_rate(); }
    int channel_count() const override { return int(m_reader->channel_count()); }
    qint64 frame_count() const override { return qint64(m_reader->file_frame_count()); }

    qint64 read(qint64 firstFrame, qint64 count, float* const* channels) override
    {
        const int channelCount = channel_count();
        const qint64 read = qint64(m_reader->read_file_frames(m_buffer, nframes_t(firstFrame), nframes_t(count)));
        const qint64 frames = std::min(read, count);
        for (int c = 0; c < channelCount; ++c) {
            std::copy_n(m_buffer.get_channel_buffer(uint(c)).get_data(nframes_t(frames)), frames, channels[c]);
        }
        return frames;
    }

private:
    std::unique_ptr<TAudioReader> m_reader;
    TFileIOBuffer m_buffer;
};


TPeakBuildTask::OpenSource TPeakBuildTask::audio_file(const QString& fileName)
{
    return [fileName](QString* error) -> std::unique_ptr<TPeakBuildSource> {
        std::unique_ptr<TAudioReader> reader = TAudioReader::open(fileName);
        if (!reader) {
            *error = QStringLiteral("cannot read %1").arg(fileName);
            return nullptr;
        }
        return std::make_unique<TAudioFileBuildSource>(std::move(reader));
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

TPeakBuildTask::TPeakBuildTask(std::shared_ptr<TPeakStream> recorded)
    : m_stream(std::move(recorded))
    , m_readFrames(0)
{
    Q_ASSERT(m_stream);
}

TPeakBuildTask::~TPeakBuildTask() = default;

bool TPeakBuildTask::fail(const QString& error)
{
    m_error = error;
    m_state = State::Failed;
    TLOG_W("peaks", "building {} failed: {}", peak_path().toStdString(), error.toStdString());
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
    if (!m_openSource) {
        // A recording: level 0 is complete
        m_state = State::Running;
        return true;
    }
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
    if (rate != m_stream->rate() || channels != m_stream->channel_count()) {
        return fail(QStringLiteral("the source has %1 Hz and %2 channels, its waveform expects %3 Hz and %4")
                        .arg(rate).arg(channels).arg(m_stream->rate()).arg(m_stream->channel_count()));
    }
    if (m_stream->open(&error) != TPeakFile::Status::Ok) {
        return fail(error);
    }

    // A second of the source: every step costs a hand-over to the
    // scheduler's pool and back (two thread wakes); a refill does not wait
    // for it, idle work never takes the last thread
    if (m_readFrames <= 0) {
        m_readFrames = qint64(rate);
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
    if (m_source) {
        return read_source();
    }

    // Level 0 is complete: the levels above it, then the file's name
    if (m_stream->post_process()) {
        return true;
    }
    if (!m_stream->published()) {
        return fail(m_stream->error());
    }
    m_progress = 100;
    m_state = State::Done;
    TLOG_I("peaks", "built {}", peak_path().toStdString());
    return false;
}

// One read into level 0; at the source's end level 0 is complete
bool TPeakBuildTask::read_source()
{
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
    } else {
        m_stream->append(m_constChannels.data(), std::min(read, toRead));
        if (m_stream->write_failed()) {
            return fail(QStringLiteral("writing the peak file failed: %1").arg(m_stream->error()));
        }
        m_position += std::min(read, toRead);
        m_progress = int(m_position * 100 / m_reportedFrames);
        if (m_position < m_reportedFrames) {
            return true;
        }
    }

    QString error;
    if (m_stream->finish(m_audioPath, &error) != TPeakFile::Status::Ok) {
        return fail(error);
    }
    release();   // the source is done; post-processing follows
    return true;
}
