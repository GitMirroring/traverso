// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "TPeakFile.h"
#include "TPeakPyramid.h"

#include <QFile>
#include <QString>

#include <atomic>
#include <memory>
#include <vector>

/**
 * @brief The peak file of one audio source while it is made: from a
 * recording (fed by its writer), a build (TPeakBuildTask, from a decoder) or
 * an import (doc/design/peak-records.md).
 *
 * Every BlockFrames frames make a level-0 record, appended to a part file of
 * its own. After every append the records are flushed and the frames they
 * cover published: another thread reads the records below that count from
 * the file (read()); nothing else is shared. finish() completes level 0, and
 * post_process() then adds the levels above it from level 0, in steps,
 * writes the header and gives the file its name. Nobody maps the part file,
 * so the thread that post-processes can rename it, also on Windows.
 *
 * When writing fails (a full disk) the stream stops writing: the count stays
 * where it was, and there is no peak file (the waveform builds it from the
 * audio later). A recording goes on regardless.
 *
 * Nothing else may write its peak file meanwhile: a source being recorded
 * gets no file waveform, so no build (TAudioClip::set_audio_source()), and
 * the builds of a peak file share one job (TPeakBuildService).
 */
class TPeakStream
{
public:
    TPeakStream(uint rate, int channelCount, QString peakPath);
    ~TPeakStream();

    uint rate() const { return m_rate; }
    int channel_count() const { return m_channelCount; }
    const QString& peak_path() const { return m_peakPath; }
    // Any thread: the frames whose level-0 records are in the file
    qint64 written_frames() const { return m_writtenFrames.load(std::memory_order_acquire); }

    // The writing thread. open() before the first append().
    TPeakFile::Status open(QString* error = nullptr);
    void append(const float* const* channels, qint64 frames);
    // Level 0 is complete, and so is the audio file (its size and time go
    // into the header): post_process() next. A stream whose writing failed
    // gives up its part file here.
    TPeakFile::Status finish(const QString& audioPath, QString* error = nullptr);
    // After finish(), one thread at a time: a piece of the levels above
    // level 0, at the end the header and the file's name. True while work
    // remains; published() says then whether the file is in place.
    bool post_process();
    bool published() const { return m_published.load(std::memory_order_acquire); }
    // Gives up the part file (a build that failed or was cancelled)
    void abandon();
    // Writing or post-processing failed: there will be no peak file
    bool write_failed() const { return m_failed; }
    const QString& error() const { return m_error; }
    // The part file (known after open())
    const QString& part_path() const { return m_partPath; }

    // Any thread but the writing one (the GUI): the level-0 blocks [first,
    // last) written so far, read from the file into records; the view has
    // level 0 only, and a window of it
    TPeakLevels read(qint64 first, qint64 last, std::vector<qint16>& records) const;

private:
    struct PostProcess;

    void close_block();
    bool write_records();
    bool fail(const QString& error);
    bool add_block(int level, const qint16* minMax, const double* meanSquares, qint64 frames);
    bool write_level(int level);

    uint m_rate;
    int m_channelCount;
    QString m_peakPath;
    QString m_partPath;
    QFile m_file;

    // The writing thread: the block being made, the records not written yet
    std::vector<float> m_min;
    std::vector<float> m_max;
    std::vector<float> m_sumSquares;
    int m_blockFill{0};
    qint64 m_frames{0};
    std::vector<qint16> m_records;
    bool m_failed{false};
    QString m_error;
    TPeakFile::SourceInfo m_source;
    std::unique_ptr<PostProcess> m_post;

    // Writer → readers: published after the records reached the file
    std::atomic<qint64> m_writtenFrames{0};
    std::atomic<bool> m_published{false};
};
