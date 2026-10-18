// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "TPeakFile.h"
#include "TPeakPyramid.h"

#include <QFile>
#include <QString>

#include <atomic>
#include <memory>

/**
 * @brief The peaks of a recording in progress, streamed to its peak file.
 *
 * The writer thread appends samples; every complete period goes to the
 * peak file's part file at once. The GUI thread takes snapshots that read
 * the written periods from a mapping of that file and the rest from memory.
 * The memory of a period is freed only after the GUI's mapping covers it,
 * so a recording holds about one period of the pyramid, not all of it.
 * finish() writes the tail and the header and renames the file into place.
 *
 * When writing fails (disk full) the recording keeps its peaks in memory
 * and has no peak file afterwards: the waveform builds it from the audio.
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

    // The writer thread. open() before the first append().
    TPeakFile::Status open(QString* error = nullptr);
    void append(const float* const* channels, qint64 frames);
    // After the audio file is complete (its size and time go into the header)
    TPeakFile::Status finish(const QString& audioPath, QString* error = nullptr);

    // The GUI thread: what is recorded so far. A snapshot is valid until the
    // next levels() call.
    TPeakLevels levels();

private:
    void remap(qint64 periods);

    TPeakPyramidBuilder m_builder;
    QString m_peakPath;

    // Writer thread
    TPeakFileWriter m_writer;
    bool m_writeFailed{false};

    // Written periods, published after they reached the file (writer → GUI)
    std::atomic<qint64> m_writtenPeriods{0};
    // Periods the GUI reads from its mapping only (GUI → writer): their
    // memory may go
    std::atomic<qint64> m_releasablePeriods{0};

    // GUI thread. QFileDevice::unmap() needs the object that mapped.
    std::unique_ptr<QFile> m_mapFile;
    uchar* m_map{nullptr};
    qint64 m_mappedPeriods{0};
};
