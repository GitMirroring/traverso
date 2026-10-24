// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TPeakFile.h"

#include "TPeakCache.h"

#include <QDateTime>
#include <QFileInfo>
#include <QTimeZone>

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>

struct TPeakFileHeader
{
    char magic[8];
    quint32 version;
    quint32 byteOrder;
    quint32 headerSize;
    quint32 rate;
    quint32 channelCount;
    quint32 blockFrames;
    quint32 codesPerOctave;     // of the value codes (TPeakLevels)
    quint32 recordContents;     // what a record has: RecordMinMax | RecordRms
    quint32 chunkLevel;         // a chunk holds 2^chunkLevel level-0 blocks
    quint32 reserved;           // 0
    quint64 frameCount;
    qint64 sourceSize;
    qint64 sourceModified;
    quint64 checksum;           // of this header with checksum 0
};

static_assert(sizeof(TPeakFileHeader) == TPeakFile::HeaderBytes && offsetof(TPeakFileHeader, frameCount) == 48);

// The chunk levels a file may have: the reader takes the file's own, the
// writer writes TPeakLevels::ChunkLevel
static constexpr quint32 MinChunkLevel = 1;
static constexpr quint32 MaxChunkLevel = 20;

// What a record holds, in this order: min/max of every channel, then the
// RMS codes of every channel. This version writes and reads exactly these.
static constexpr quint32 RecordMinMax = 1;
static constexpr quint32 RecordRms = 2;
static constexpr quint32 RecordContents = RecordMinMax | RecordRms;

static constexpr char Magic[8] = {'T', 'R', 'A', 'V', 'P', 'E', 'A', 'K'};
static constexpr quint32 ByteOrderMark = 0x01020304;   // stored in the writer's byte order

// FNV-1a over 32-bit words. Not a cryptographic hash: it only has to notice
// a damaged header.
static quint64 header_checksum(TPeakFileHeader header)
{
    header.checksum = 0;
    quint64 checksum = 14695981039346656037ull;
    const auto* p = reinterpret_cast<const uchar*>(&header);
    for (size_t i = 0; i + 4 <= sizeof(header); i += 4) {
        quint32 word;
        std::memcpy(&word, p + i, sizeof(word));
        checksum ^= word;
        checksum *= 1099511628211ull;
    }
    return checksum;
}

QByteArray TPeakFile::header(const SourceInfo& source)
{
    TPeakFileHeader header{};
    std::memcpy(header.magic, Magic, sizeof(Magic));
    header.version = FormatVersion;
    header.byteOrder = ByteOrderMark;
    header.headerSize = sizeof(TPeakFileHeader);
    header.rate = source.rate;
    header.channelCount = quint32(source.channelCount);
    header.blockFrames = TPeakLevels::BlockFrames;
    header.codesPerOctave = quint32(TPeakLevels::CodesPerOctave);
    header.recordContents = RecordContents;
    header.chunkLevel = TPeakLevels::ChunkLevel;
    header.frameCount = quint64(source.frameCount);
    header.sourceSize = source.fileSize;
    header.sourceModified = source.modified;
    header.checksum = header_checksum(header);
    return QByteArray(reinterpret_cast<const char*>(&header), sizeof(header));
}


TPeakFile::SourceInfo TPeakFile::SourceInfo::from_file(const QString& sourcePath, uint rate, int channelCount, qint64 frameCount)
{
    const QFileInfo info(sourcePath);
    SourceInfo source;
    source.rate = rate;
    source.channelCount = channelCount;
    source.frameCount = frameCount;
    source.fileSize = info.size();
    // In UTC: the same milliseconds, without a time zone lookup (libc's
    // time zone state is shared by every thread that asks)
    source.modified = info.lastModified(QTimeZone::UTC).toMSecsSinceEpoch();
    return source;
}

TPeakFile::~TPeakFile()
{
    close();
}

TPeakFile::Status TPeakFile::open(const QString& path, const SourceInfo& expected)
{
    close();

    m_file.setFileName(path);
    if (!m_file.exists()) {
        return Status::Missing;
    }
    if (!m_file.open(QIODevice::ReadOnly)) {
        return Status::Error;
    }
    if (m_file.size() < HeaderBytes) {
        m_file.close();
        return Status::Invalid;
    }

    const qint64 size = m_file.size();
    m_map = m_file.map(0, size);
    // The mapping does not need the descriptor (QFileDevice::map), so an
    // open peak file costs no file descriptor
    m_file.close();
    if (!m_map) {
        return Status::Error;
    }

    const Status status = validate(expected, size);
    if (status != Status::Ok) {
        close();
        return status;
    }
    // In use: the cache's eviction leaves it alone
    TPeakCache::opened(path);
    m_registered = true;
    return status;
}

TPeakFile::Status TPeakFile::validate(const SourceInfo& expected, qint64 fileSize)
{
    TPeakFileHeader header;
    std::memcpy(&header, m_map, sizeof(header));

    // An unfinished file has a header of zeros
    if (std::memcmp(header.magic, Magic, sizeof(Magic)) != 0
            || header.version != FormatVersion
            || header.byteOrder != ByteOrderMark
            || header.headerSize != sizeof(TPeakFileHeader)
            || header.blockFrames != quint32(TPeakLevels::BlockFrames)
            || header.codesPerOctave != quint32(TPeakLevels::CodesPerOctave)
            || header.recordContents != RecordContents
            || header.chunkLevel < MinChunkLevel || header.chunkLevel > MaxChunkLevel
            || header.reserved != 0
            || header.checksum != header_checksum(header)) {
        return Status::Invalid;
    }

    // A decoder can report more frames than it delivers; the build records
    // the frames that were really there, so the file may hold fewer than the
    // source reports.
    if (header.frameCount == 0
            || header.rate != expected.rate
            || header.channelCount != quint32(expected.channelCount)
            || expected.frameCount < 0
            || header.frameCount > quint64(expected.frameCount)
            || header.sourceSize != expected.fileSize
            || std::abs(header.sourceModified - expected.modified) > SourceTimeTolerance) {
        return Status::Invalid;
    }

    const int chunkLevel = int(header.chunkLevel);
    const qint64 chunks = chunk_count(qint64(header.frameCount), chunkLevel);
    if (HeaderBytes + chunks * chunk_bytes(chunkLevel, expected.channelCount) != fileSize) {
        return Status::Invalid;
    }

    m_levels = TPeakLevels(expected.rate, expected.channelCount, qint64(header.frameCount), chunkLevel,
                           reinterpret_cast<const qint16*>(m_map + HeaderBytes), chunks);
    return Status::Ok;
}

void TPeakFile::close()
{
    if (m_map) {
        m_file.unmap(m_map);
        m_map = nullptr;
    }
    m_levels = TPeakLevels();
    if (m_registered) {
        TPeakCache::closed(m_file.fileName());
        m_registered = false;
    }
}

TPeakFile::Status TPeakFile::publish(const QString& part, const QString& path, QString* error)
{
    // Replaces an older file at once (POSIX rename): whoever has the old one
    // mapped keeps reading it. On Windows a file that is open or mapped can
    // be neither renamed nor replaced. Paths go through Qt's std::filesystem
    // conversion: native wide names on Windows, so no character is lost.
    std::error_code renameError;
    std::filesystem::rename(QFileInfo(part).filesystemFilePath(), QFileInfo(path).filesystemFilePath(), renameError);
    if (renameError) {
        if (error) {
            *error = QString::fromStdString(renameError.message());
        }
        return Status::Error;
    }
    return Status::Ok;
}
