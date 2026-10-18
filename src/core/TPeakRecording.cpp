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
            // Keep everything in memory from here on; no peak file
            TLOG_W("peaks", "writing {} failed: {}", m_peakPath.toStdString(), error.toStdString());
            m_writeFailed = true;
            m_writer.close();
            return;
        }
        m_writtenPeriods.store(periods, std::memory_order_release);
    }

    // The GUI reads these periods from the file now
    m_builder.release_periods(std::min(m_releasablePeriods.load(std::memory_order_acquire), m_writer.written_periods()));
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
    const TPeakFile::Status status = m_writer.finish(m_builder.levels(), source, error);
    m_writeFailed = status != TPeakFile::Status::Ok;
    return status;
}

void TPeakRecording::remap(qint64 periods)
{
    // The part file grows; after finish() it has its final name. The new
    // mapping comes first: when it fails, the old one and memory still
    // serve every period.
    auto file = std::make_unique<QFile>(TPeakFileWriter::part_path(m_peakPath));
    if (!file->exists()) {
        file->setFileName(m_peakPath);
    }
    const qint64 size = TPeakFileWriter::records_end(m_builder.period_shift(), m_builder.channel_count(), periods);
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

TPeakLevels TPeakRecording::levels()
{
    // The previous snapshot is no longer in use: what it read from the
    // mapping only may leave memory
    m_releasablePeriods.store(m_mappedPeriods, std::memory_order_release);

    const qint64 written = m_writtenPeriods.load(std::memory_order_acquire);
    if (written > m_mappedPeriods) {
        remap(written);
    }

    const TPeakLevels memory = m_builder.levels();
    const int shift = m_builder.period_shift();
    std::vector<TPeakLevels::Level> levels(size_t(memory.level_count()));
    for (int level = 0; level < memory.level_count(); ++level) {
        TPeakLevels::Level& out = levels[size_t(level)];
        out.blockCount = memory.block_count(level);
        if (level > shift) {
            // Not in records: memory only, in the builder's chunks (of
            // MinChunkShift blocks above the period shift)
            out.chunkShift = TPeakPyramidBuilder::MinChunkShift;
            for (int channel = 0; channel < memory.channel_count(); ++channel) {
                std::vector<const TPeakBlock*> pointers;
                for (qint64 index = 0; index < out.blockCount;) {
                    qint64 count = 0;
                    pointers.push_back(memory.blocks(level, channel, index, count));
                    index += count;
                }
                out.channels.push_back(std::move(pointers));
            }
            continue;
        }

        // One chunk per period: from the mapping while it covers the period
        out.chunkShift = shift - level;
        const qint64 periodBlocks = qint64(1) << out.chunkShift;
        const qint64 chunks = (out.blockCount + periodBlocks - 1) / periodBlocks;
        for (int channel = 0; channel < memory.channel_count(); ++channel) {
            std::vector<const TPeakBlock*> pointers(static_cast<size_t>(chunks));
            for (qint64 period = 0; period < chunks; ++period) {
                if (period < m_mappedPeriods) {
                    pointers[size_t(period)] = reinterpret_cast<const TPeakBlock*>(
                        m_map + TPeakFileWriter::record_offset(shift, memory.channel_count(), period, level, channel));
                } else {
                    qint64 count = 0;
                    pointers[size_t(period)] = memory.blocks(level, channel, period * periodBlocks, count);
                }
            }
            out.channels.push_back(std::move(pointers));
        }
    }

    return TPeakLevels(memory.rate(), memory.channel_count(), memory.frame_count(), std::move(levels));
}
