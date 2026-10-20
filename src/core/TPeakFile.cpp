// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TPeakFile.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QTimeZone>

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
    quint32 codesPerOctave;     // of the value codes (TPeakLevels)
    quint32 periodShift;
    quint32 planes;             // what each block has: PlaneMinMax | PlaneRms
    quint32 padding;            // 0
    quint64 frameCount;     // 0: unfinished
    qint64 sourceSize;
    qint64 sourceModified;
    quint64 checksum;       // of this header with checksum 0
};

static_assert(sizeof(TPeakFileHeader) == 80 && offsetof(TPeakFileHeader, frameCount) == 48);
static_assert(sizeof(TPeakBlock) == 4);

// The planes of a block, in the order a record or the tail holds them per
// level: min/max of every channel, then the RMS codes of every channel.
// This version writes and reads exactly these.
static constexpr quint32 PlaneMinMax = 1;
static constexpr quint32 PlaneRms = 2;
static constexpr quint32 Planes = PlaneMinMax | PlaneRms;

static constexpr char Magic[8] = {'T', 'R', 'A', 'V', 'P', 'E', 'A', 'K'};
static constexpr quint32 ByteOrderMark = 0x01020304;   // stored in the writer's byte order
static constexpr qint64 DataStart = qint64(sizeof(TPeakFileHeader));
static constexpr qint64 BlockBytes = qint64(sizeof(TPeakBlock));
static constexpr qint64 RmsBytes = qint64(sizeof(qint16));
static constexpr qint64 PlanesBytes = BlockBytes + RmsBytes;   // per block and channel

// FNV-1a over 32-bit words. Not a cryptographic hash: it only has to notice
// a damaged header.
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

// A damaged period shift or frame count can keep the file size the same
static quint64 header_checksum(TPeakFileHeader header)
{
    header.checksum = 0;
    return add_to_checksum(ChecksumSeed, &header, qint64(sizeof(header)));
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
 * Where everything is in a version 6 file: follows from the period shift,
 * the channel count and the block count of every level. In a record and in
 * the tail every level holds its min/max of every channel, then its RMS
 * codes of every channel.
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
            recordBytes += period_blocks(level) * PlanesBytes * channelCount;
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

    // Where level starts in record period, and its min/max and RMS of channel
    qint64 record_level(qint64 period, int level) const
    {
        qint64 offset = DataStart + period * recordBytes;
        for (int l = 0; l < level; ++l) {
            offset += period_blocks(l) * PlanesBytes * channelCount;
        }
        return offset;
    }
    qint64 record_offset(qint64 period, int level, int channel) const
    {
        return record_level(period, level) + channel * period_blocks(level) * BlockBytes;
    }
    qint64 record_rms_offset(qint64 period, int level, int channel) const
    {
        return record_level(period, level) + period_blocks(level) * (channelCount * BlockBytes + channel * RmsBytes);
    }

    qint64 tail_start() const { return DataStart + periods * recordBytes; }

    // The same for the tail; tail_level(level_count()) is the end of the file
    qint64 tail_level(int level) const
    {
        qint64 offset = tail_start();
        for (int l = 0; l < level; ++l) {
            offset += tail_blocks(l) * PlanesBytes * channelCount;
        }
        return offset;
    }
    qint64 tail_offset(int level, int channel) const
    {
        return tail_level(level) + channel * tail_blocks(level) * BlockBytes;
    }
    qint64 tail_rms_offset(int level, int channel) const
    {
        return tail_level(level) + tail_blocks(level) * (channelCount * BlockBytes + channel * RmsBytes);
    }

    qint64 end() const { return tail_level(level_count()); }
};


qint64 TPeakFileWriter::record_offset(int periodShift, int channelCount, qint64 period, int level, int channel)
{
    return TPeakFileLayout(periodShift, channelCount, {}).record_offset(period, level, channel);
}

qint64 TPeakFileWriter::record_rms_offset(int periodShift, int channelCount, qint64 period, int level, int channel)
{
    return TPeakFileLayout(periodShift, channelCount, {}).record_rms_offset(period, level, channel);
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
    // In UTC: the same milliseconds, without a time zone lookup (libc's
    // time zone state is shared by every thread that asks)
    source.modified = info.lastModified(QTimeZone::UTC).toMSecsSinceEpoch();
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
    Status status = writer.open(path, levels.rate(), levels.channel_count(), periodShift, error);
    if (status != Status::Ok) {
        return status;
    }
    status = writer.finish(levels, source, error);
    if (status != Status::Ok) {
        return status;
    }
    return TPeakFileWriter::publish(path, error);
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
            || header.codesPerOctave != quint32(TPeakLevels::CodesPerOctave)
            || header.periodShift > quint32(TPeakPyramidBuilder::MaxPeriodShift)
            || header.planes != Planes
            || header.checksum != header_checksum(header)) {
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
            std::vector<const qint16*> rms;
            if (level <= layout.periodShift) {
                // One chunk per record, the last one in the tail
                out.chunkShift = layout.periodShift - level;
                for (qint64 period = 0; period < layout.periods; ++period) {
                    chunks.push_back(reinterpret_cast<const TPeakBlock*>(m_map + layout.record_offset(period, level, channel)));
                    rms.push_back(reinterpret_cast<const qint16*>(m_map + layout.record_rms_offset(period, level, channel)));
                }
            }
            if (layout.tail_blocks(level) > 0) {
                chunks.push_back(reinterpret_cast<const TPeakBlock*>(m_map + layout.tail_offset(level, channel)));
                rms.push_back(reinterpret_cast<const qint16*>(m_map + layout.tail_rms_offset(level, channel)));
            }
            out.add_channel(chunks, rms);
        }
    }

    m_levels = TPeakLevels(expected.rate, expected.channelCount, qint64(header.frameCount), std::move(levels));
    return Status::Ok;
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
    header.codesPerOctave = quint32(TPeakLevels::CodesPerOctave);
    header.periodShift = quint32(periodShift);
    header.planes = Planes;
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
        first += count;
    }
    return true;
}

bool TPeakFileWriter::write_rms(const TPeakLevels& levels, int level, int channel, qint64 first, qint64 last)
{
    while (first < last) {
        qint64 count = 0;
        const qint16* run = levels.rms_blocks(level, channel, first, count);
        count = std::min(count, last - first);
        const qint64 bytes = count * RmsBytes;
        if (m_file.write(reinterpret_cast<const char*>(run), bytes) != bytes) {
            return false;
        }
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
            for (int channel = 0; channel < m_channelCount; ++channel) {
                if (!write_rms(levels, level, channel, first, first + blocks)) {
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
        for (int channel = 0; channel < m_channelCount; ++channel) {
            if (!write_rms(levels, level, channel, layout.record_blocks(level), layout.counts[size_t(level)])) {
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
    header.codesPerOctave = quint32(TPeakLevels::CodesPerOctave);
    header.periodShift = quint32(m_periodShift);
    header.planes = Planes;
    header.frameCount = quint64(source.frameCount);
    header.sourceSize = source.fileSize;
    header.sourceModified = source.modified;
    header.checksum = header_checksum(header);
    if (!m_file.seek(0) || m_file.write(reinterpret_cast<const char*>(&header), sizeof(header)) != qint64(sizeof(header))) {
        return failed(error);
    }

    // Flush first: close() would clear the error text of a failed write
    if (!m_file.flush()) {
        return failed(error);
    }
    m_file.close();
    m_finished = true;   // the part file stays for publish()
    return TPeakFile::Status::Ok;
}

TPeakFile::Status TPeakFileWriter::publish(const QString& path, QString* error)
{
    const QString part = part_path(path);

    // Replaces an older file at once (POSIX rename): whoever has the old one
    // mapped keeps reading it. On Windows a file that is mapped can be
    // neither renamed nor replaced: the caller releases its own mapping of
    // the part file first (TPeakStream::publish()), and a build that meets
    // an old file another process has mapped reports an error and is built
    // again later. Paths go through Qt's std::filesystem conversion: native
    // wide names on Windows, so no character is lost.
    std::error_code renameError;
    std::filesystem::rename(QFileInfo(part).filesystemFilePath(), QFileInfo(path).filesystemFilePath(), renameError);
    if (renameError) {
        if (error) {
            *error = QString::fromStdString(renameError.message());
        }
        QFile::remove(part);
        return TPeakFile::Status::Error;
    }
    return TPeakFile::Status::Ok;
}
