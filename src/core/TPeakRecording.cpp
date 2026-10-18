// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TPeakRecording.h"

#include "TLog.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>

TPeakRecording::TPeakRecording(uint rate, int channelCount, QString peakPath, int periodShift)
    : m_builder(rate, channelCount, periodShift)
    , m_peakPath(std::move(peakPath))
{
    // The record layout is fixed by the period shift and the channel count
    m_dataStart = TPeakFileWriter::records_end(periodShift, channelCount, 0);
    m_recordBytes = TPeakFileWriter::records_end(periodShift, channelCount, 1) - m_dataStart;
    for (int level = 0; level <= periodShift; ++level) {
        m_levelOffsets.push_back(TPeakFileWriter::record_offset(periodShift, channelCount, 0, level, 0) - m_dataStart);
        m_rmsOffsets.push_back(TPeakFileWriter::record_rms_offset(periodShift, channelCount, 0, level, 0) - m_dataStart);
    }
}

TPeakRecording::~TPeakRecording()
{
    if (m_map) {
        m_mapFile->unmap(m_map);
    }
}

TPeakFile::Status TPeakRecording::open(QString* error)
{
    if (!QDir().mkpath(QFileInfo(m_peakPath).absolutePath())) {
        if (error) {
            *error = QStringLiteral("cannot create the peak file directory");
        }
        m_writeFailed = true;
        return TPeakFile::Status::Error;
    }
    const TPeakFile::Status status = m_writer.open(m_peakPath, m_builder.rate(), m_builder.channel_count(),
                                                   m_builder.period_shift(), error);
    m_writeFailed = status != TPeakFile::Status::Ok;
    return status;
}

void TPeakRecording::append(const float* const* channels, qint64 frames)
{
    m_builder.append(channels, frames);
    if (m_writeFailed) {
        return;
    }

    const qint64 periods = m_builder.complete_periods();
    if (periods > m_writer.written_periods()) {
        QString error;
        if (m_writer.write_periods(m_builder.levels(), periods, &error) != TPeakFile::Status::Ok) {
            // Keep everything in memory from here on; no peak file. Periods
            // already released live on in the reader's mapping of the part
            // file, which close() removes: on POSIX the mapping keeps the
            // data, on Windows the removal fails and leaves the part file.
            TLOG_W("peaks", "writing {} failed: {}", m_peakPath.toStdString(), error.toStdString());
            m_writeFailed = true;
            m_writer.close();
            return;
        }
        m_writtenPeriods.store(periods, std::memory_order_seq_cst);
    }

    // Free the written periods while nobody reads (see the class comment);
    // with a read going on the next append tries again
    if (m_readers.load(std::memory_order_seq_cst) == 0) {
        m_builder.release_periods(m_writer.written_periods());
    }
}

TPeakFile::Status TPeakRecording::finish(const QString& audioPath, QString* error)
{
    m_builder.finish();
    if (m_writeFailed) {
        if (error) {
            *error = QStringLiteral("the peak file could not be written");
        }
        return TPeakFile::Status::Error;
    }
    const TPeakFile::SourceInfo source = TPeakFile::SourceInfo::from_file(
        audioPath, m_builder.rate(), m_builder.channel_count(), m_builder.frame_count());
    TPeakFile::Status status = m_writer.finish(m_builder.levels(), source, error);
    if (status == TPeakFile::Status::Ok) {
        status = TPeakFileWriter::publish(m_peakPath, error);
    }
    m_writeFailed = status != TPeakFile::Status::Ok;
    if (!m_writeFailed) {
        m_writtenPeriods.store(m_writer.written_periods(), std::memory_order_seq_cst);
    }
    return status;
}

void TPeakRecording::remap(qint64 periods)
{
    // The part file grows; after finish() it has its final name. The new
    // mapping comes first: when it fails, the old one stays.
    auto file = std::make_unique<QFile>(TPeakFileWriter::part_path(m_peakPath));
    if (!file->exists()) {
        file->setFileName(m_peakPath);
    }
    const qint64 size = m_dataStart + periods * m_recordBytes;
    if (!file->open(QIODevice::ReadOnly) || file->size() < size) {
        return;
    }
    uchar* map = file->map(0, size);
    // A mapping does not need the descriptor
    file->close();
    if (!map) {
        return;
    }
    if (m_map) {
        m_mapFile->unmap(m_map);
    }
    m_mapFile = std::move(file);
    m_map = map;
    m_mappedPeriods = periods;
}

const TPeakBlock* TPeakRecording::mapped_blocks(qint64 period, int level, int channel) const
{
    const qint64 periodBlocks = qint64(1) << (m_builder.period_shift() - level);
    return reinterpret_cast<const TPeakBlock*>(m_map + m_dataStart + period * m_recordBytes + m_levelOffsets[size_t(level)]
                                               + channel * periodBlocks * qint64(sizeof(TPeakBlock)));
}

const qint16* TPeakRecording::mapped_rms(qint64 period, int level, int channel) const
{
    const qint64 periodBlocks = qint64(1) << (m_builder.period_shift() - level);
    return reinterpret_cast<const qint16*>(m_map + m_dataStart + period * m_recordBytes + m_rmsOffsets[size_t(level)]
                                           + channel * periodBlocks * qint64(sizeof(qint16)));
}

TPeakLevels TPeakRecording::snapshot()
{
    const qint64 written = m_writtenPeriods.load(std::memory_order_seq_cst);
    if (written > m_mappedPeriods) {
        remap(written);
    }

    const TPeakLevels memory = m_builder.levels();
    const int shift = m_builder.period_shift();
    const int channels = memory.channel_count();

    // Written periods may only be read from the mapping. When it does not
    // reach them all yet (a failed remap), the snapshot ends where it does.
    const bool capped = m_mappedPeriods < written;
    const qint64 cappedBlocks = m_mappedPeriods << shift;   // level 0

    std::vector<TPeakLevels::Level> levels;
    for (int level = 0; level < memory.level_count(); ++level) {
        qint64 blockCount = memory.block_count(level);
        if (capped) {
            blockCount = std::min(blockCount, cappedBlocks >> level);
        }
        if (blockCount == 0) {
            break;
        }

        TPeakLevels::Level out;
        out.blockCount = blockCount;
        if (level > shift) {
            // Not in records: memory only, in the builder's chunks (of
            // MinChunkShift blocks above the period shift), never freed
            out.chunkShift = TPeakPyramidBuilder::MinChunkShift;
            for (int channel = 0; channel < channels; ++channel) {
                std::vector<const TPeakBlock*> pointers;
                std::vector<const qint16*> rms;
                for (qint64 index = 0; index < blockCount;) {
                    qint64 count = 0;
                    pointers.push_back(memory.blocks(level, channel, index, count));
                    rms.push_back(memory.rms_blocks(level, channel, index, count));
                    index += count;
                }
                out.add_channel(pointers, rms);
            }
        } else {
            // One chunk per period: the written ones from the mapping
            out.chunkShift = shift - level;
            const qint64 periodBlocks = qint64(1) << out.chunkShift;
            const qint64 chunks = (blockCount + periodBlocks - 1) / periodBlocks;
            for (int channel = 0; channel < channels; ++channel) {
                std::vector<const TPeakBlock*> pointers(static_cast<size_t>(chunks));
                std::vector<const qint16*> rms(static_cast<size_t>(chunks));
                for (qint64 period = 0; period < chunks; ++period) {
                    if (period < written) {
                        pointers[size_t(period)] = mapped_blocks(period, level, channel);
                        rms[size_t(period)] = mapped_rms(period, level, channel);
                    } else {
                        qint64 count = 0;
                        pointers[size_t(period)] = memory.blocks(level, channel, period * periodBlocks, count);
                        rms[size_t(period)] = memory.rms_blocks(level, channel, period * periodBlocks, count);
                    }
                }
                out.add_channel(pointers, rms);
            }
        }
        levels.push_back(std::move(out));
    }

    const qint64 frames = capped ? std::min(memory.frame_count(), cappedBlocks * TPeakLevels::BlockFrames) : memory.frame_count();
    return TPeakLevels(memory.rate(), channels, frames, std::move(levels));
}
