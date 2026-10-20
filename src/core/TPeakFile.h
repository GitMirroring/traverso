// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "TPeakPyramid.h"

#include <QByteArray>
#include <QFile>
#include <QString>

#include <vector>

/**
 * @brief Peak file (TRAVPEAK version 3): a TPeakLevels pyramid on disk, read
 * mapped. TPeakStream writes it.
 *
 * The file is a cache of its source: when it is missing, unfinished, from
 * another version, or does not match the source (rate, channels, frames, file
 * size, modification time), open() says so and the caller rebuilds it. An
 * open peak file holds a mapping, not a file descriptor.
 *
 * Layout (doc/design/peak-records.md): a header of fixed size, then the
 * records of every level, level 0 first, one record per block (the min/max
 * of every channel, then the RMS of every channel). Every offset follows
 * from the frame count, so there is no table. The header is written last;
 * until then it is zero, so an unfinished file is Invalid. It has a
 * checksum, which open() checks. The records have none: a file reaches its
 * name only when it is complete, so what opens is whole. Peak files are a
 * cache: they are not synced to the disk, a file damaged by a power loss
 * shows a wrong waveform until it is built again.
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

    // Checks the header, its checksum and the size. An open peak file holds
    // a mapping, no file descriptor, and is registered with the peak cache
    // while it is open (TPeakCache::opened()), so the cache's eviction
    // leaves it alone: every open() anywhere touches that registry.
    Status open(const QString& path, const SourceInfo& expected);
    void close();

    bool is_open() const { return m_map != nullptr; }
    // Valid while the file is open
    const TPeakLevels& levels() const { return m_levels; }

    // Magic TRAVPEAK, version counted from 2 (the format after legacy TRAVPF
    // 1.5; version 2 had the levels in periods). Every format change counts up.
    static constexpr quint32 FormatVersion = 3;
    // How far the source's time may be from the one in the header: network
    // shares and FAT round times to 1 or 2 seconds
    static constexpr qint64 SourceTimeTolerance = 2000;   // ms

    // The layout, for TPeakStream: the header's size, a record's size, the
    // blocks of every level of a pyramid over frames frames
    static constexpr qint64 HeaderBytes = 72;
    static qint64 record_bytes(int channelCount) { return qint64(TPeakLevels::record_codes(channelCount)) * qint64(sizeof(qint16)); }
    static std::vector<qint64> block_counts(qint64 frames);
    // The header of the finished file of source
    static QByteArray header(const SourceInfo& source);

    // Renames the finished part file over path, so a file under its name is
    // always complete. Not synced to the disk: a peak file is a cache. On
    // Windows a file that another program holds open for a moment (a virus
    // scanner, the indexer) cannot be renamed: it tries again a few times.
    // A part file that cannot be renamed is removed.
    static Status publish(const QString& partPath, const QString& path, QString* error = nullptr);

private:
    Status validate(const SourceInfo& expected, qint64 fileSize);

    QFile m_file;
    uchar* m_map{nullptr};
    TPeakLevels m_levels;
    bool m_registered{false};   // with TPeakCache, while open
};
