// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TPeakFile.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <limits>
#include <vector>

struct TPeakFileHeader
{
    char magic[8];
    quint32 version;
    quint32 byteOrder;
    quint32 headerSize;
    quint32 rate;
    quint32 channelCount;
    quint32 blockFrames;
    quint32 valueScale;
    quint32 periodShift;
    quint64 frameCount;     // 0: unfinished
    qint64 sourceSize;
    qint64 sourceModified;
    quint64 checksum;       // of the block data in file order, then of this header with checksum 0
};

static_assert(sizeof(TPeakFileHeader) == 72 && offsetof(TPeakFileHeader, frameCount) == 40);
static_assert(sizeof(TPeakBlock) == 4);

static constexpr char Magic[8] = {'T', 'R', 'A', 'V', 'P', 'E', 'A', 'K'};
static constexpr quint32 ByteOrderMark = 0x01020304;   // stored in the writer's byte order
static constexpr qint64 DataStart = qint64(sizeof(TPeakFileHeader));
static constexpr qint64 BlockBytes = qint64(sizeof(TPeakBlock));

// FNV-1a over 32-bit words: block data always is a whole number of blocks.
// Not a cryptographic hash: it only has to notice data that is missing,
// zeroed or damaged.
static quint64 add_to_checksum(quint64 checksum, const void* data, qint64 bytes)
{
    const auto* p = static_cast<const uchar*>(data);
    for (qint64 i = 0; i + 4 <= bytes; i += 4) {
        quint32 word;
        std::memcpy(&word, p + i, sizeof(word));
        checksum ^= word;
        checksum *= 1099511628211ull;
    }
    return checksum;
}
static constexpr quint64 ChecksumSeed = 14695981039346656037ull;

// The header goes into the checksum too (with its checksum field 0): a
// damaged period shift or frame count can keep the file size the same
static quint64 add_header_to_checksum(quint64 checksum, TPeakFileHeader header)
{
    header.checksum = 0;
    return add_to_checksum(checksum, &header, qint64(sizeof(header)));
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

/**
 * Where everything is in a version 3 file: follows from the period shift,
 * the channel count and the block count of every level.
 */
struct TPeakFileLayout
{
    int periodShift{0};
    int channelCount{0};
    std::vector<qint64> counts;     // blocks per level
    qint64 periods{0};              // complete periods: one record each
    qint64 recordBytes{0};

    TPeakFileLayout(int shift, int channels, std::vector<qint64> blockCounts)
        : periodShift(shift)
        , channelCount(channels)
        , counts(std::move(blockCounts))
    {
        for (int level = 0; level <= periodShift; ++level) {
            recordBytes += period_blocks(level) * BlockBytes * channelCount;
        }
        periods = complete_periods(periodShift, counts);
    }

    // The periods every level 0..shift has complete
    static qint64 complete_periods(int shift, const std::vector<qint64>& counts)
    {
        qint64 periods = std::numeric_limits<qint64>::max();
        for (int level = 0; level <= shift; ++level) {
            const qint64 blocks = level < int(counts.size()) ? counts[size_t(level)] : 0;
            periods = std::min(periods, blocks >> (shift - level));
        }
        return periods;
    }

    int level_count() const { return int(counts.size()); }
    qint64 period_blocks(int level) const { return qint64(1) << (periodShift - level); }

    // Blocks of level in the records, and in the tail
    qint64 record_blocks(int level) const { return level <= periodShift ? periods * period_blocks(level) : 0; }
    qint64 tail_blocks(int level) const { return counts[size_t(level)] - record_blocks(level); }

    qint64 record_offset(qint64 period, int level, int channel) const
    {
        qint64 offset = DataStart + period * recordBytes;
        for (int l = 0; l < level; ++l) {
            offset += period_blocks(l) * BlockBytes * channelCount;
        }
        return offset + channel * period_blocks(level) * BlockBytes;
    }

    qint64 tail_start() const { return DataStart + periods * recordBytes; }

    qint64 tail_offset(int level, int channel) const
    {
        qint64 offset = tail_start();
        for (int l = 0; l < level; ++l) {
            offset += tail_blocks(l) * BlockBytes * channelCount;
        }
        return offset + channel * tail_blocks(level) * BlockBytes;
    }

    qint64 end() const { return level_count() > 0 ? tail_offset(level_count() - 1, channelCount) : tail_start(); }
};


qint64 TPeakFileWriter::record_offset(int periodShift, int channelCount, qint64 period, int level, int channel)
{
    return TPeakFileLayout(periodShift, channelCount, {}).record_offset(period, level, channel);
}

qint64 TPeakFileWriter::records_end(int periodShift, int channelCount, qint64 periods)
{
    return DataStart + periods * TPeakFileLayout(periodShift, channelCount, {}).recordBytes;
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

TPeakFile::Status TPeakFile::write(const QString& path, const TPeakLevels& levels, const SourceInfo& source, QString* error,
                                   int periodShift)
{
    TPeakFileWriter writer;
    const Status status = writer.open(path, levels.rate(), levels.channel_count(), periodShift, error);
    if (status != Status::Ok) {
        return status;
    }
    return writer.finish(levels, source, error);
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
    }
    return status;
}

TPeakFile::Status TPeakFile::validate(const SourceInfo& expected, qint64 fileSize)
{
    TPeakFileHeader header;
    std::memcpy(&header, m_map, sizeof(header));

    if (std::memcmp(header.magic, Magic, sizeof(Magic)) != 0
            || header.version != FormatVersion
            || header.byteOrder != ByteOrderMark
            || header.headerSize != sizeof(TPeakFileHeader)
            || header.blockFrames != quint32(TPeakLevels::BlockFrames)
            || header.valueScale != quint32(TPeakLevels::ValueScale)
            || header.periodShift > quint32(TPeakPyramidBuilder::MaxPeriodShift)) {
        return Status::Invalid;
    }

    // Frame count 0: never finished. A decoder can report more frames than
    // it delivers; the build records the frames that were really there, so
    // the file may hold fewer than the source reports.
    if (header.frameCount == 0
            || header.rate != expected.rate
            || header.channelCount != quint32(expected.channelCount)
            || expected.frameCount < 0
            || header.frameCount > quint64(expected.frameCount)
            || header.sourceSize != expected.fileSize
            || header.sourceModified != expected.modified) {
        return Status::Invalid;
    }

    const TPeakFileLayout layout(int(header.periodShift), expected.channelCount, expected_block_counts(qint64(header.frameCount)));
    if (layout.end() != fileSize) {
        return Status::Invalid;
    }

    std::vector<TPeakLevels::Level> levels(size_t(layout.level_count()));
    for (int level = 0; level < layout.level_count(); ++level) {
        TPeakLevels::Level& out = levels[size_t(level)];
        out.blockCount = layout.counts[size_t(level)];
        for (int channel = 0; channel < expected.channelCount; ++channel) {
            std::vector<const TPeakBlock*> chunks;
            if (level <= layout.periodShift) {
                // One chunk per record, the last one in the tail
                out.chunkShift = layout.periodShift - level;
                for (qint64 period = 0; period < layout.periods; ++period) {
                    chunks.push_back(reinterpret_cast<const TPeakBlock*>(m_map + layout.record_offset(period, level, channel)));
                }
            }
            if (layout.tail_blocks(level) > 0) {
                chunks.push_back(reinterpret_cast<const TPeakBlock*>(m_map + layout.tail_offset(level, channel)));
            }
            out.channels.push_back(std::move(chunks));
        }
    }

    m_dataStart = DataStart;
    m_dataEnd = fileSize;

    m_levels = TPeakLevels(expected.rate, expected.channelCount, qint64(header.frameCount), std::move(levels));
    return Status::Ok;
}

bool TPeakFile::verify_checksum() const
{
    return m_map && checksum_matches(checksum_add(checksum_begin(), 0, data_size()));
}

quint64 TPeakFile::checksum_begin()
{
    return ChecksumSeed;
}

quint64 TPeakFile::checksum_add(quint64 checksum, qint64 offset, qint64 bytes) const
{
    Q_ASSERT(m_map && offset >= 0 && offset + bytes <= data_size() && offset % 4 == 0 && bytes % 4 == 0);
    return add_to_checksum(checksum, m_map + m_dataStart + offset, bytes);
}

bool TPeakFile::checksum_matches(quint64 checksum) const
{
    if (!m_map) {
        return false;
    }
    TPeakFileHeader header;
    std::memcpy(&header, m_map, sizeof(header));
    return add_header_to_checksum(checksum, header) == header.checksum;
}

void TPeakFile::close()
{
    if (m_map) {
        m_file.unmap(m_map);
        m_map = nullptr;
    }
    m_levels = TPeakLevels();
}


TPeakFileWriter::~TPeakFileWriter()
{
    close();
}

void TPeakFileWriter::close()
{
    if (m_file.isOpen()) {
        m_file.close();
    }
    if (!m_finished && !m_path.isEmpty()) {
        QFile::remove(part_path(m_path));
    }
}

TPeakFile::Status TPeakFileWriter::failed(QString* error)
{
    if (error) {
        *error = m_file.errorString();
    }
    close();
    return TPeakFile::Status::Error;
}

TPeakFile::Status TPeakFileWriter::open(const QString& path, uint rate, int channelCount, int periodShift, QString* error)
{
    Q_ASSERT(rate > 0 && channelCount > 0);
    Q_ASSERT(periodShift >= 0 && periodShift <= TPeakPyramidBuilder::MaxPeriodShift);

    m_rate = rate;
    m_channelCount = channelCount;
    m_periodShift = periodShift;
    m_writtenPeriods = 0;
    m_checksum = ChecksumSeed;
    m_finished = false;

    m_path = path;
    m_file.setFileName(part_path(path));
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return failed(error);
    }

    TPeakFileHeader header{};
    std::memcpy(header.magic, Magic, sizeof(Magic));
    header.version = TPeakFile::FormatVersion;
    header.byteOrder = ByteOrderMark;
    header.headerSize = sizeof(TPeakFileHeader);
    header.rate = rate;
    header.channelCount = quint32(channelCount);
    header.blockFrames = TPeakLevels::BlockFrames;
    header.valueScale = quint32(TPeakLevels::ValueScale);
    header.periodShift = quint32(periodShift);
    // frameCount 0: unfinished
    if (m_file.write(reinterpret_cast<const char*>(&header), sizeof(header)) != qint64(sizeof(header))) {
        return failed(error);
    }
    return TPeakFile::Status::Ok;
}

bool TPeakFileWriter::write_blocks(const TPeakLevels& levels, int level, int channel, qint64 first, qint64 last)
{
    while (first < last) {
        qint64 count = 0;
        const TPeakBlock* run = levels.blocks(level, channel, first, count);
        count = std::min(count, last - first);
        const qint64 bytes = count * BlockBytes;
        if (m_file.write(reinterpret_cast<const char*>(run), bytes) != bytes) {
            return false;
        }
        m_checksum = add_to_checksum(m_checksum, run, bytes);
        first += count;
    }
    return true;
}

TPeakFile::Status TPeakFileWriter::write_periods(const TPeakLevels& levels, qint64 periods, QString* error)
{
    Q_ASSERT(m_file.isOpen() && !m_finished);
    Q_ASSERT(levels.channel_count() == m_channelCount);

    for (; m_writtenPeriods < periods; ++m_writtenPeriods) {
        for (int level = 0; level <= m_periodShift; ++level) {
            const qint64 blocks = qint64(1) << (m_periodShift - level);
            const qint64 first = m_writtenPeriods * blocks;
            Q_ASSERT(level < levels.level_count() && levels.block_count(level) >= first + blocks);
            for (int channel = 0; channel < m_channelCount; ++channel) {
                if (!write_blocks(levels, level, channel, first, first + blocks)) {
                    return failed(error);
                }
            }
        }
    }
    // Out of QFile's buffer: a record is in the file (and visible to a
    // mapping of it) as soon as it is written
    if (!m_file.flush()) {
        return failed(error);
    }
    return TPeakFile::Status::Ok;
}

TPeakFile::Status TPeakFileWriter::finish(const TPeakLevels& levels, const TPeakFile::SourceInfo& source, QString* error)
{
    Q_ASSERT(m_file.isOpen() && !m_finished);

    const std::vector<qint64> counts = expected_block_counts(levels.frame_count());
    bool complete = !counts.empty() && int(counts.size()) == levels.level_count();
    for (int level = 0; complete && level < levels.level_count(); ++level) {
        complete = counts[size_t(level)] == levels.block_count(level);
    }
    if (!complete || levels.rate() != m_rate || levels.channel_count() != m_channelCount
            || source.rate != m_rate || source.channelCount != m_channelCount
            || levels.frame_count() != source.frameCount) {
        if (error) {
            *error = QStringLiteral("the pyramid is empty, incomplete or does not belong to the source");
        }
        close();
        return TPeakFile::Status::Invalid;
    }

    const TPeakFileLayout layout(m_periodShift, m_channelCount, counts);

    // The records not written yet: a pyramid written in one go, or the
    // periods the last appends completed
    TPeakFile::Status status = write_periods(levels, layout.periods, error);
    if (status != TPeakFile::Status::Ok) {
        return status;
    }

    // The tail: the rest of every level, level by level, channel by channel
    for (int level = 0; level < layout.level_count(); ++level) {
        for (int channel = 0; channel < m_channelCount; ++channel) {
            if (!write_blocks(levels, level, channel, layout.record_blocks(level), layout.counts[size_t(level)])) {
                return failed(error);
            }
        }
    }
    Q_ASSERT(m_file.pos() == layout.end());

    TPeakFileHeader header{};
    std::memcpy(header.magic, Magic, sizeof(Magic));
    header.version = TPeakFile::FormatVersion;
    header.byteOrder = ByteOrderMark;
    header.headerSize = sizeof(TPeakFileHeader);
    header.rate = m_rate;
    header.channelCount = quint32(m_channelCount);
    header.blockFrames = TPeakLevels::BlockFrames;
    header.valueScale = quint32(TPeakLevels::ValueScale);
    header.periodShift = quint32(m_periodShift);
    header.frameCount = quint64(source.frameCount);
    header.sourceSize = source.fileSize;
    header.sourceModified = source.modified;
    header.checksum = add_header_to_checksum(m_checksum, header);
    if (!m_file.seek(0) || m_file.write(reinterpret_cast<const char*>(&header), sizeof(header)) != qint64(sizeof(header))) {
        return failed(error);
    }

    // No fsync: the file is a cache, the checksum tells whether it is whole.
    // Flush first: close() would clear the error text of a failed write.
    if (!m_file.flush()) {
        return failed(error);
    }
    m_file.close();

    // Replaces an older file at once (POSIX rename): whoever has the old one
    // mapped keeps reading it. On Windows replacing a file that is mapped
    // fails; such a build reports an error and the file is built again later.
    std::error_code renameError;
    std::filesystem::rename(QFile::encodeName(part_path(m_path)).toStdString(), QFile::encodeName(m_path).toStdString(), renameError);
    if (renameError) {
        if (error) {
            *error = QString::fromStdString(renameError.message());
        }
        close();
        return TPeakFile::Status::Error;
    }
    m_finished = true;
    return TPeakFile::Status::Ok;
}
