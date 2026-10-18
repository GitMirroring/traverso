// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "TPeakFile.h"
#include "TPeakPyramid.h"
#include "TWaveformColumns.h"

#include <QString>

#include <atomic>
#include <chrono>
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
 * @brief Builds the peak file of one audio source, a slice at a time.
 *
 * Whoever runs it calls step() with a time budget until it returns false,
 * then publish() when the file is Written. The first step opens the
 * source, so the decoder is opened on the thread that builds. Every
 * complete period goes to the peak file at once and its memory is
 * released, so a build holds about one period of the pyramid. State, error
 * and progress belong to the thread that steps (and then to the one that
 * publishes); cancel() may be called from any thread.
 */
class TPeakBuildTask
{
public:
    using OpenSource = std::function<std::unique_ptr<TPeakBuildSource>(QString* error)>;

    enum class State
    {
        Waiting,    // not started
        Running,
        Written,    // the part file is complete: publish() next
        Done,       // the peak file is in place
        Failed,     // see error()
        Unsupported,    // more channels than MaxChannels
        Cancelled
    };

    // A source with more channels than this cannot be read (TFileIOBuffer)
    static constexpr int MaxChannels = 2;
    static bool supports(int channelCount) { return channelCount <= MaxChannels; }

    TPeakBuildTask(OpenSource openSource, QString audioPath, QString peakPath,
                   int periodShift = TPeakPyramidBuilder::DefaultPeriodShift, qint64 readFrames = 16384);
    ~TPeakBuildTask();

    // Works until budget is used or the build ends; true while work remains
    bool step(std::chrono::nanoseconds budget);
    // After step() ended in Written: waits until the file is on the disk and
    // puts it under its name (TPeakFileWriter::publish()). Blocks, so not
    // on a thread that serves audio streams.
    State publish();
    void cancel() { m_cancelled.store(true, std::memory_order_relaxed); }

    State state() const { return m_state; }
    const QString& error() const { return m_error; }
    int progress() const { return m_progress; }
    const QString& peak_path() const { return m_peakPath; }

    // Opens an audio file with Traverso's decoders, at the file's own rate
    static OpenSource audio_file(const QString& fileName);

private:
    bool start();
    bool finish();
    bool fail(const QString& error, State state = State::Failed);
    void release();

    OpenSource m_openSource;
    QString m_audioPath;
    QString m_peakPath;
    int m_periodShift;
    qint64 m_readFrames;

    std::unique_ptr<TPeakBuildSource> m_source;
    std::unique_ptr<TPeakPyramidBuilder> m_builder;
    TPeakFileWriter m_writer;
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
