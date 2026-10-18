// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "TPeakPyramid.h"

#include <QFile>
#include <QString>

/**
 * @brief Peak file format 3: a TPeakLevels pyramid on disk, written as a
 * stream, read mapped.
 *
 * The file is a cache of its source: when it is missing, unfinished, from
 * another version, or does not match the source (rate, channels, frames, file
 * size, modification time), open() says so and the caller rebuilds it. An
 * open peak file holds a mapping, not a file descriptor.
 *
 * Layout (doc/design/peak-diskio.md): a header of fixed size, then one record
 * per complete period (TPeakPyramidBuilder: the blocks of levels 0..S of that
 * period, level by level, channel by channel), then a tail with the rest of
 * every level. Every offset follows from the header, so there is no table.
 * The header says unfinished (frame count 0) until TPeakFileWriter::finish()
 * writes the frame count, the source and the checksum of the block data.
 */
class TPeakFile
{
public:
    // What the file must match
    struct SourceInfo
    {
        uint rate{0};
        int channelCount{0};
        qint64 frameCount{0};
        qint64 fileSize{0};
        qint64 modified{0};   // ms since epoch, UTC

        // rate, channels and frames from the reader, size and time from the file
        static SourceInfo from_file(const QString& sourcePath, uint rate, int channelCount, qint64 frameCount);
    };

    enum class Status
    {
        Ok,
        Missing,    // no file: build it
        Invalid,    // unfinished, other version, other source or damaged: build it again
        Error       // the file system refused (permissions, mapping, disk full)
    };

    TPeakFile() = default;
    ~TPeakFile();
    TPeakFile(const TPeakFile&) = delete;
    TPeakFile& operator=(const TPeakFile&) = delete;

    // Writes a complete pyramid (TPeakPyramidBuilder::finish()) in one go
    static Status write(const QString& path, const TPeakLevels& levels, const SourceInfo& source, QString* error = nullptr,
                        int periodShift = TPeakPyramidBuilder::DefaultPeriodShift);

    // Checks the header and the layout, not the block data: see verify_checksum()
    Status open(const QString& path, const SourceInfo& expected);
    void close();

    bool is_open() const { return m_map != nullptr; }
    // Valid while the file is open
    const TPeakLevels& levels() const { return m_levels; }

    // Whether the header and the block data match the checksum. Reads all of the
    // file (which also brings it into the page cache), so not for the GUI
    // thread.
    bool verify_checksum() const;

    static constexpr quint32 FormatVersion = 3;

private:
    Status validate(const SourceInfo& expected, qint64 fileSize);

    QFile m_file;
    uchar* m_map{nullptr};
    qint64 m_dataStart{0};
    qint64 m_dataEnd{0};
    TPeakLevels m_levels;
};

/**
 * @brief Writes a peak file as a stream: the header at once, a record as
 * soon as a period is complete, the tail and the final header at the end.
 *
 * The file is written in place. Until finish() its header says unfinished,
 * so a crash or a cancelled build leaves a file that open() refuses.
 */
class TPeakFileWriter
{
public:
    TPeakFileWriter() = default;
    ~TPeakFileWriter();
    TPeakFileWriter(const TPeakFileWriter&) = delete;
    TPeakFileWriter& operator=(const TPeakFileWriter&) = delete;

    TPeakFile::Status open(const QString& path, uint rate, int channelCount,
                           int periodShift = TPeakPyramidBuilder::DefaultPeriodShift, QString* error = nullptr);

    // Writes the records of the periods [written_periods(), periods); levels
    // must hold them (TPeakPyramidBuilder::complete_periods())
    TPeakFile::Status write_periods(const TPeakLevels& levels, qint64 periods, QString* error = nullptr);

    // levels must be complete (TPeakPyramidBuilder::finish()) and belong to
    // source: writes the remaining records, the tail and the final header
    TPeakFile::Status finish(const TPeakLevels& levels, const TPeakFile::SourceInfo& source, QString* error = nullptr);

    int period_shift() const { return m_periodShift; }
    qint64 written_periods() const { return m_writtenPeriods; }

private:
    bool write_blocks(const TPeakLevels& levels, int level, int channel, qint64 first, qint64 last);
    TPeakFile::Status failed(QString* error);

    QFile m_file;
    uint m_rate{0};
    int m_channelCount{0};
    int m_periodShift{TPeakPyramidBuilder::DefaultPeriodShift};
    qint64 m_writtenPeriods{0};
    quint64 m_checksum{0};
    bool m_finished{false};
};
