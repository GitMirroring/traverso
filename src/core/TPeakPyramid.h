// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QtGlobal>

#include <atomic>
#include <memory>
#include <vector>

// The lowest and highest sample value of a block of frames, scaled by
// TPeakLevels::ValueScale and rounded outwards, so the stored span always
// contains the real one.
struct TPeakBlock
{
    qint16 min;
    qint16 max;
};

/**
 * @brief Read-only view on a min/max pyramid of one audio source.
 *
 * Level 0 holds one block per BlockFrames frames of the source, at the
 * source's own sample rate. Block k of level L covers the frames
 * [k * BlockFrames * 2^L, (k + 1) * BlockFrames * 2^L), and is the combination
 * of blocks 2k and 2k + 1 of level L - 1. The last block of a level may cover
 * fewer frames. A level may miss its last block while a recording grows (a
 * block is only added when both its children are complete).
 *
 * The blocks of a level live in chunks of 2^chunkShift blocks: one chunk
 * for a mapped file, many for a growing pyramid (which never moves a block).
 * The view does not own the data: TPeakPyramidBuilder (memory) and TPeakFile
 * (mapped file) do. A view taken from a growing pyramid is a snapshot: the
 * blocks it names never change.
 */
class TPeakLevels
{
public:
    static constexpr int BlockFrames = 64;
    static constexpr int BlockFramesShift = 6;
    static constexpr float ValueScale = 8192.0f;   // 1.0 = full scale, range +-4.0 (+12 dBFS)
    static constexpr int MaxValue = 32767;

    struct Level
    {
        qint64 blockCount{0};
        int chunkShift{62};                                      // 62: everything in one chunk
        std::vector<std::vector<const TPeakBlock*>> channels;    // [channel][chunk]
    };

    TPeakLevels() = default;
    TPeakLevels(uint rate, int channelCount, qint64 frameCount, std::vector<Level> levels);

    uint rate() const { return m_rate; }
    int channel_count() const { return m_channelCount; }
    qint64 frame_count() const { return m_frameCount; }

    // 0 when there is no pyramid (yet); frame_count() and rate() still tell
    // about the source then
    int level_count() const { return int(m_levels.size()); }
    qint64 block_count(int level) const { return m_levels[size_t(level)].blockCount; }
    const TPeakBlock& block(int level, int channel, qint64 index) const
    {
        Q_ASSERT(index >= 0 && index < block_count(level));
        const Level& l = m_levels[size_t(level)];
        return l.channels[size_t(channel)][size_t(index >> l.chunkShift)][index & ((qint64(1) << l.chunkShift) - 1)];
    }
    // The blocks from index on that follow each other in memory: their
    // start and how many (at least 1 when index < block_count(level))
    const TPeakBlock* blocks(int level, int channel, qint64 index, qint64& count) const;

    // The combined block of the level-0 blocks [first, last) of channel,
    // from the coarsest levels that fit. first < last <= block_count(0).
    TPeakBlock range(int channel, qint64 first, qint64 last) const;

    static qint16 quantize_min(float value);
    static qint16 quantize_max(float value);
    static float to_value(qint16 stored) { return float(stored) / ValueScale; }

private:
    uint m_rate{0};
    int m_channelCount{0};
    qint64 m_frameCount{0};
    std::vector<Level> m_levels;
};

/**
 * @brief Builds a TPeakLevels pyramid from samples, in chunks of any size.
 *
 * The same code serves a background import (whole file) and a recording
 * (buffer by buffer): every level grows as soon as its blocks are complete.
 * finish() closes the last partial blocks.
 *
 * One thread appends; any thread may take levels() meanwhile: a block is
 * written once, then its level's count is published (release), and a reader's
 * snapshot (acquire) only names blocks below that count. Blocks never move
 * (chunked storage) and live until the builder is destroyed. No locks.
 *
 * The storage of a level L <= period_shift() is chunked per period (or in
 * chunks of several whole periods), so a period's blocks of one level and
 * channel follow each other in memory.
 */
class TPeakPyramidBuilder
{
public:
    static constexpr int MaxLevels = 40;
    // 2^34 frames, a day at 192 kHz. The audio readers stop sooner, at 2^32
    // frames (6.2 hours at 192 kHz, 24.8 at 48 kHz): their positions are
    // 32-bit.
    static constexpr qint64 MaxLevel0Blocks = qint64(1) << 28;

    // A period is 2^periodShift level-0 blocks. Within a period every level
    // L <= periodShift has exactly 2^(periodShift - L) blocks, so all those
    // levels complete it at the same moment: the record of a peak file.
    static constexpr int DefaultPeriodShift = 14;   // 2^20 frames, ~22 s at 48 kHz
    static constexpr int MaxPeriodShift = 24;
    // Storage chunks hold at least 2^MinChunkShift blocks (4 KB)
    static constexpr int MinChunkShift = 10;

    TPeakPyramidBuilder(uint rate, int channelCount, int periodShift = DefaultPeriodShift);

    // channels[c] points to frames samples of channel c
    void append(const float* const* channels, qint64 frames);
    void finish();

    bool is_finished() const { return m_finished; }
    uint rate() const { return m_rate; }
    int channel_count() const { return int(m_channels.size()); }
    int period_shift() const { return m_periodShift; }
    qint64 frame_count() const { return m_publishedFrames.load(std::memory_order_acquire); }

    // The periods that every level 0..period_shift() has completed. For the
    // thread that appends, or any thread once a finished pyramid was handed
    // over to it.
    qint64 complete_periods() const;

    // Frees the storage of levels 0..period_shift() that only holds blocks of
    // the periods before periods (once they are in the peak file). Only for
    // the thread that appends, and only when no snapshot that names those
    // blocks is in use anymore: later snapshots name them with null chunks.
    void release_periods(qint64 periods);
    // Blocks of storage allocated and not released. Only for the thread that
    // appends.
    qint64 allocated_blocks() const;

    // A snapshot of what is complete now; valid while the builder lives
    TPeakLevels levels() const;

private:
    // The published chunk pointers live in pages of PageSize pointers. The
    // page directory is sized once and a page is allocated before its first
    // chunk is published, so nothing a snapshot reads ever moves, and the
    // pointers cost memory only for the chunks that exist.
    static constexpr int PageShift = 6;
    static constexpr qint64 PageSize = qint64(1) << PageShift;

    struct LevelStore
    {
        int chunkShift{MinChunkShift};
        std::vector<std::unique_ptr<TPeakBlock[]>> chunks;          // the writer's
        // Sized once, read by snapshots. Atomic entries: release_periods()
        // clears the entries of released chunks while a snapshot may read them
        std::vector<std::unique_ptr<std::atomic<const TPeakBlock*>[]>> pages;
        qint64 releasedChunks{0};
        qint64 count{0};
        std::atomic<qint64> publishedCount{0};

        const TPeakBlock& at(qint64 index) const
        {
            return chunks[size_t(index >> chunkShift)][index & ((qint64(1) << chunkShift) - 1)];
        }
        const TPeakBlock* published_chunk(qint64 chunk) const
        {
            return pages[size_t(chunk >> PageShift)][chunk & (PageSize - 1)].load(std::memory_order_relaxed);
        }
    };

    struct Channel
    {
        float min;
        float max;
        std::vector<LevelStore> levels;   // MaxLevels, so nothing ever resizes
    };

    void add_block(Channel& channel, int level, TPeakBlock block);
    void close_partial_block();

    uint m_rate;
    int m_periodShift;
    std::vector<Channel> m_channels;
    qint64 m_frameCount{0};
    std::atomic<qint64> m_publishedFrames{0};
    int m_blockFill{0};   // frames in the current level-0 block
    bool m_finished{false};
    bool m_full{false};
};
