// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "TPeakFile.h"
#include "TPeakPyramid.h"
#include "TPeakStream.h"
#include "TWaveformColumns.h"

#include <QString>

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

/**
 * @brief What a peak build reads: an audio source at its own rate.
 *
 * read() returns the frames read: 0 at the end of the source (which can come
 * before frame_count(): decoders can report more frames than they deliver).
 */
class TPeakBuildSource : public TWaveformSampleSource
{
public:
    virtual uint rate() const = 0;
    virtual int channel_count() const = 0;
    // As the source reports it
    virtual qint64 frame_count() const = 0;
};

/**
 * @brief Makes the peak file of one audio source in a TPeakStream, one step
 * at a time: a build reads the source into level 0, then post-processes;
 * a recording's stream (level 0 complete) is post-processed only.
 *
 * Whoever runs it calls step() until it returns false; each step reads
 * about a fifth of a second of the source (a reader's refill, so the steps
 * of a DiskIO round take about as long as each other), or post-processes a
 * piece. The first step opens the source, so the decoder is opened on the
 * thread that builds. A reader of the stream sees the waveform grow. The
 * last step puts the file under its name. State, error and progress belong
 * to the thread that steps; cancel() may be called from any thread.
 */
class TPeakBuildTask
{
public:
    using OpenSource = std::function<std::unique_ptr<TPeakBuildSource>(QString* error)>;

    enum class State
    {
        Waiting,    // not started
        Running,
        Done,       // the peak file is in place
        Failed,     // see error()
        Unsupported,    // more channels than MaxChannels
        Cancelled
    };

    // A source with more channels than this cannot be read (TFileIOBuffer)
    static constexpr int MaxChannels = 2;
    static bool supports(int channelCount) { return channelCount <= MaxChannels; }

    // stream: where the peaks go, at the rate and channel count the source
    // must have (its waveform knows them). readFrames: frames per step, 0
    // for a fifth of a second of the source.
    TPeakBuildTask(OpenSource openSource, QString audioPath, std::shared_ptr<TPeakStream> stream, qint64 readFrames = 0);
    // A recording's stream after its finish(): post-processing only
    explicit TPeakBuildTask(std::shared_ptr<TPeakStream> recorded);
    ~TPeakBuildTask();

    // One read of the source into the stream, or a piece of
    // post-processing; true while work remains
    bool step();
    void cancel() { m_cancelled.store(true, std::memory_order_relaxed); }

    State state() const { return m_state; }
    const QString& error() const { return m_error; }
    int progress() const { return m_progress; }
    const QString& peak_path() const { return m_stream->peak_path(); }
    const std::shared_ptr<TPeakStream>& stream() const { return m_stream; }

    // Opens an audio file with Traverso's decoders, at the file's own rate
    static OpenSource audio_file(const QString& fileName);

private:
    bool start();
    bool read_source();
    bool fail(const QString& error, State state = State::Failed);
    void release();

    OpenSource m_openSource;
    QString m_audioPath;
    std::shared_ptr<TPeakStream> m_stream;
    qint64 m_readFrames;

    std::unique_ptr<TPeakBuildSource> m_source;
    std::vector<std::vector<float>> m_samples;
    std::vector<float*> m_channels;
    std::vector<const float*> m_constChannels;
    qint64 m_reportedFrames{0};
    qint64 m_position{0};

    std::atomic<bool> m_cancelled{false};
    State m_state{State::Waiting};
    QString m_error;
    int m_progress{0};
};
