// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TPeakFile.h"

#include <QDateTime>
#include <QFileInfo>
#include <QSaveFile>

#include <cstddef>
#include <cstring>

struct TPeakFileHeader
{
    char magic[8];
    quint32 version;
    quint32 byteOrder;
    quint32 headerSize;    // including the level table
    quint32 rate;
    quint32 channelCount;
    quint32 blockFrames;
    quint32 valueScale;
    quint32 levelCount;
    quint64 frameCount;
    qint64 sourceSize;
    qint64 sourceModified;
};

struct TPeakFileLevel
{
    quint64 blockCount;
    quint64 offset;        // from the start of the file; channels follow each other
};

static_assert(sizeof(TPeakFileHeader) == 64 && offsetof(TPeakFileHeader, frameCount) == 40);
static_assert(sizeof(TPeakFileLevel) == 16);
static_assert(sizeof(TPeakBlock) == 4);

static constexpr char Magic[8] = {'T', 'R', 'A', 'V', 'P', 'E', 'A', 'K'};
static constexpr quint32 ByteOrderMark = 0x01020304;   // stored in the writer's byte order
static constexpr int MaxLevels = 63;

static qint64 align8(qint64 value)
{
    return (value + 7) & ~qint64(7);
}

// The block count of every level of a complete pyramid over frames frames
static std::vector<qint64> expected_block_counts(qint64 frames)
{
    std::vector<qint64> counts;
    if (frames <= 0) {
        return counts;
    }
    qint64 blocks = (frames + TPeakLevels::BlockFrames - 1) >> TPeakLevels::BlockFramesShift;
    counts.push_back(blocks);
    while (blocks > 1) {
        blocks = (blocks + 1) / 2;
        counts.push_back(blocks);
    }
    return counts;
}


TPeakFile::SourceInfo TPeakFile::SourceInfo::from_file(const QString& sourcePath, uint rate, int channelCount, qint64 frameCount)
{
    const QFileInfo info(sourcePath);
    SourceInfo source;
    source.rate = rate;
    source.channelCount = channelCount;
    source.frameCount = frameCount;
    source.fileSize = info.size();
    source.modified = info.lastModified().toMSecsSinceEpoch();
    return source;
}

TPeakFile::~TPeakFile()
{
    close();
}

TPeakFile::Status TPeakFile::write(const QString& path, const TPeakLevels& levels, const SourceInfo& source, QString* error)
{
    const std::vector<qint64> counts = expected_block_counts(levels.frame_count());
    bool complete = int(counts.size()) == levels.level_count();
    for (int level = 0; complete && level < levels.level_count(); ++level) {
        complete = counts[size_t(level)] == levels.block_count(level);
    }
    if (!complete || levels.rate() != source.rate || levels.channel_count() != source.channelCount
            || levels.frame_count() != source.frameCount) {
        if (error) {
            *error = QStringLiteral("the pyramid is incomplete or does not belong to the source");
        }
        return Status::Invalid;
    }

    TPeakFileHeader header{};
    std::memcpy(header.magic, Magic, sizeof(Magic));
    header.version = FormatVersion;
    header.byteOrder = ByteOrderMark;
    header.headerSize = quint32(sizeof(TPeakFileHeader) + sizeof(TPeakFileLevel) * size_t(levels.level_count()));
    header.rate = source.rate;
    header.channelCount = quint32(source.channelCount);
    header.blockFrames = TPeakLevels::BlockFrames;
    header.valueScale = quint32(TPeakLevels::ValueScale);
    header.levelCount = quint32(levels.level_count());
    header.frameCount = quint64(source.frameCount);
    header.sourceSize = source.fileSize;
    header.sourceModified = source.modified;

    std::vector<TPeakFileLevel> table(size_t(levels.level_count()));
    qint64 offset = align8(header.headerSize);
    for (int level = 0; level < levels.level_count(); ++level) {
        table[size_t(level)] = {quint64(levels.block_count(level)), quint64(offset)};
        offset = align8(offset + levels.block_count(level) * qint64(sizeof(TPeakBlock)) * levels.channel_count());
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = file.errorString();
        }
        return Status::Error;
    }

    static const char padding[8] = {};
    qint64 position = 0;
    auto write_bytes = [&](const void* data, qint64 size) {
        if (file.write(static_cast<const char*>(data), size) != size) {
            return false;
        }
        position += size;
        return true;
    };
    auto pad_to = [&](qint64 target) {
        return write_bytes(padding, target - position);
    };

    bool ok = write_bytes(&header, sizeof(header))
           && write_bytes(table.data(), qint64(sizeof(TPeakFileLevel) * table.size()));
    for (int level = 0; ok && level < levels.level_count(); ++level) {
        ok = pad_to(qint64(table[size_t(level)].offset));
        for (int channel = 0; ok && channel < levels.channel_count(); ++channel) {
            ok = write_bytes(&levels.block(level, channel, 0), levels.block_count(level) * qint64(sizeof(TPeakBlock)));
        }
    }
    ok = ok && pad_to(offset) && file.commit();

    if (!ok) {
        if (error) {
            *error = file.errorString();
        }
        file.cancelWriting();
        return Status::Error;
    }
    return Status::Ok;
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
    if (m_file.size() < qint64(sizeof(TPeakFileHeader))) {
        m_file.close();
        return Status::Invalid;
    }

    m_map = m_file.map(0, m_file.size());
    if (!m_map) {
        m_file.close();
        return Status::Error;
    }

    const Status status = validate(expected);
    if (status != Status::Ok) {
        close();
    }
    return status;
}

TPeakFile::Status TPeakFile::validate(const SourceInfo& expected)
{
    const qint64 fileSize = m_file.size();

    TPeakFileHeader header;
    std::memcpy(&header, m_map, sizeof(header));

    if (std::memcmp(header.magic, Magic, sizeof(Magic)) != 0
            || header.version != FormatVersion
            || header.byteOrder != ByteOrderMark
            || header.blockFrames != quint32(TPeakLevels::BlockFrames)
            || header.valueScale != quint32(TPeakLevels::ValueScale)
            || header.levelCount > quint32(MaxLevels)
            || header.headerSize != sizeof(TPeakFileHeader) + sizeof(TPeakFileLevel) * header.levelCount
            || qint64(header.headerSize) > fileSize) {
        return Status::Invalid;
    }

    if (header.rate != expected.rate
            || header.channelCount != quint32(expected.channelCount)
            || header.frameCount != quint64(expected.frameCount)
            || header.sourceSize != expected.fileSize
            || header.sourceModified != expected.modified) {
        return Status::Invalid;
    }

    const std::vector<qint64> counts = expected_block_counts(expected.frameCount);
    if (header.levelCount != counts.size()) {
        return Status::Invalid;
    }

    std::vector<TPeakLevels::Level> levels(counts.size());
    for (size_t level = 0; level < counts.size(); ++level) {
        TPeakFileLevel entry;
        std::memcpy(&entry, m_map + sizeof(TPeakFileHeader) + level * sizeof(TPeakFileLevel), sizeof(entry));

        const qint64 channelBytes = counts[level] * qint64(sizeof(TPeakBlock));
        if (entry.blockCount != quint64(counts[level])
                || entry.offset < header.headerSize
                || (entry.offset & 7) != 0
                || qint64(entry.offset) > fileSize - channelBytes * expected.channelCount) {
            return Status::Invalid;
        }

        levels[level].blockCount = counts[level];
        for (int channel = 0; channel < expected.channelCount; ++channel) {
            levels[level].channels.push_back(
                reinterpret_cast<const TPeakBlock*>(m_map + entry.offset + quint64(channelBytes) * quint64(channel)));
        }
    }

    m_levels = TPeakLevels(expected.rate, expected.channelCount, expected.frameCount, std::move(levels));
    return Status::Ok;
}

void TPeakFile::close()
{
    if (m_map) {
        m_file.unmap(m_map);
        m_map = nullptr;
    }
    m_file.close();
    m_levels = TPeakLevels();
}
