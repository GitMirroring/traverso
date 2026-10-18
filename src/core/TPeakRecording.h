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
 * @brief The peaks of a recording in progress, streamed to its peak file.
 *
 * The writer thread appends samples; every complete period goes to the peak
 * file's part file at once, and its memory goes as soon as nobody reads: a
 * recording holds about one period of peaks, whether anything looks at it
 * or not. finish() writes the tail and the header, waits until the file is
 * on the disk and renames it into place.
 *
 * Readers (the GUI thread) read inside read(): the snapshot it hands out
 * names the written periods in a mapping of the file and only the periods
 * not written yet in memory, and it is valid during the call only, so copy
 * what you need. While a read is going on the writer keeps the memory; it
 * frees it on a later append. (A reader announces itself before it looks at
 * the written count, the writer publishes the count before it looks for
 * readers: either the writer sees the reader and waits, or the reader sees
 * the count and does not touch that memory.)
 *
 * Nothing else may write its peak file meanwhile: a source being recorded
 * gets no file waveform, so no build (TAudioClip::set_audio_source()).
 *
 * When writing fails (disk full) the recording keeps the peaks it has not
 * written in memory, still reads the written ones from its mapping, and has
 * no peak file afterwards: the waveform builds it from the audio.
 */
class TPeakRecording
{
public:
    TPeakRecording(uint rate, int channelCount, QString peakPath,
                   int periodShift = TPeakPyramidBuilder::DefaultPeriodShift);
    ~TPeakRecording();

    uint rate() const { return m_builder.rate(); }
    int channel_count() const { return m_builder.channel_count(); }
    const QString& peak_path() const { return m_peakPath; }
    // Frames recorded so far (any thread)
    qint64 recorded_frames() const { return m_builder.frame_count(); }
    // Level-0 blocks recorded so far (any thread): a read() now has them
    qint64 recorded_blocks() const { return m_builder.published_blocks(); }

    // The writer thread. open() before the first append().
    TPeakFile::Status open(QString* error = nullptr);
    void append(const float* const* channels, qint64 frames);
    // After the audio file is complete (its size and time go into the
    // header). Waits until the peak file is on the disk: the recording has
    // stopped, this thread has nothing else to do.
    TPeakFile::Status finish(const QString& audioPath, QString* error = nullptr);
    // Blocks the writer holds in memory (for tests and diagnostics)
    qint64 memory_blocks() const { return m_builder.allocated_blocks(); }

    // The reading thread (the GUI): f(const TPeakLevels&) with what is
    // recorded so far
    template <typename F>
    void read(F&& f)
    {
        m_readers.fetch_add(1, std::memory_order_seq_cst);
        struct Leave
        {
            std::atomic<int>& readers;
            ~Leave() { readers.fetch_sub(1, std::memory_order_seq_cst); }
        } leave{m_readers};
        const TPeakLevels levels = snapshot();
        f(levels);
    }

private:
    TPeakLevels snapshot();
    void remap(qint64 periods);
    const TPeakBlock* mapped_blocks(qint64 period, int level, int channel) const;
    const qint16* mapped_rms(qint64 period, int level, int channel) const;

    TPeakPyramidBuilder m_builder;
    QString m_peakPath;

    // Writer thread
    TPeakFileWriter m_writer;
    bool m_writeFailed{false};

    // Written periods, published after they reached the file (writer → readers)
    std::atomic<qint64> m_writtenPeriods{0};
    // Reads going on (readers → writer): memory of written periods is freed
    // only while there are none
    std::atomic<int> m_readers{0};

    // Reading thread. QFileDevice::unmap() needs the object that mapped.
    std::unique_ptr<QFile> m_mapFile;
    uchar* m_map{nullptr};
    qint64 m_mappedPeriods{0};

    // Where the blocks and the RMS codes of a level start inside a record,
    // per level 0..S
    std::vector<qint64> m_levelOffsets;
    std::vector<qint64> m_rmsOffsets;
    qint64 m_recordBytes{0};
    qint64 m_dataStart{0};
};
