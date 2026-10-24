// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "TPeakFile.h"
#include "TPeakPyramid.h"

#include <QElapsedTimer>
#include <QFile>
#include <QString>

#include <atomic>
#include <functional>
#include <vector>

/**
 * @brief The peak file of one audio source while it is made: from a
 * recording (fed by its writer), a build (TPeakBuildTask, from a decoder) or
 * an import (doc/design/peak-records.md).
 *
 * Every BlockFrames frames make a level-0 record, appended to a part file of
 * its own; the levels above it are made as the blocks come (a waiting block
 * per level, a pair makes the block above it) and follow a chunk's level 0
 * when the chunk is full (TPeakLevels). After every append the records are
 * flushed and the frames they cover published: another thread reads what is
 * below that count from the file (read()); nothing else is shared. The
 * complete chunks follow from the count: the last block of a chunk and the
 * chunk's levels above it are written together. finish() completes the last
 * chunk, post_process() writes the header and gives the file its name. A
 * reader maps the file only for one read, so the rename meets no mapping
 * between reads, also on Windows.
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
    // The audio is complete, and so is the audio file (its size and time go
    // into the header): the last chunk, filled up with zeros; post_process()
    // next. A stream whose writing failed gives up its part file here.
    TPeakFile::Status finish(const QString& audioPath, QString* error = nullptr);
    // After finish(), one thread at a time: the header and the file's name.
    // True while work remains (on Windows a rename that is refused is tried
    // again for a while); published() says then whether the file is in place.
    bool post_process();
    bool published() const { return m_published.load(std::memory_order_acquire); }
    // Gives up the part file (a build that failed or was cancelled)
    void abandon();
    // Writing or post-processing failed: there will be no peak file
    bool write_failed() const { return m_failed; }
    const QString& error() const { return m_error; }
    // The part file (known after open())
    const QString& part_path() const { return m_partPath; }

    // Any thread but the writing one (the GUI): use(levels) with what is
    // written so far, its complete chunks and level 0 of the chunk being
    // written. The file is mapped for this call only, so between reads
    // nothing keeps it open (TPeakFile::publish()). No blocks yet, or no
    // file: levels without frames.
    void read(const std::function<void(const TPeakLevels&)>& use) const;

private:
    // A block of a level that waits for its pair: its min/max codes, mean
    // squares and frames
    struct Waiting
    {
        std::vector<qint16> minMax;
        std::vector<double> squares;
        qint64 frames{0};
        bool waiting{false};
    };

    void close_block();
    void add_block(int level, const qint16* minMax, const double* meanSquares, qint64 frames);
    void close_chunk();
    bool write_records();
    bool fail(const QString& error);

    uint m_rate;
    int m_channelCount;
    QString m_peakPath;
    QString m_partPath;
    QFile m_file;

    // The writing thread: the block being made, the records not written yet
    // (in the order of the file)
    std::vector<float> m_min;
    std::vector<float> m_max;
    std::vector<float> m_sumSquares;
    int m_blockFill{0};
    qint64 m_frames{0};
    std::vector<qint16> m_records;
    // The chunk being made: its level-0 blocks so far, its levels above level
    // 0 (zeros where there is no block yet), each level's records so far,
    // and per level below the top the block that waits for its pair
    qint64 m_chunkBlocks{0};
    std::vector<qint16> m_upper;
    std::vector<qint64> m_upperFill;
    std::vector<Waiting> m_waiting;
    std::vector<double> m_squares;
    bool m_failed{false};
    QString m_error;
    TPeakFile::SourceInfo m_source;
    // post_process(): the header is written, since when a rename is refused
    bool m_headerWritten{false};
    QElapsedTimer m_renaming;

    // Writer → readers: published after the records reached the file
    std::atomic<qint64> m_writtenFrames{0};
    std::atomic<bool> m_published{false};
};
