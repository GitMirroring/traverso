// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QtGlobal>

#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

// The lowest and highest sample value of a block of frames as value codes
// (TPeakLevels::to_value()), rounded outwards, so the stored span always
// contains the real one.
struct TPeakBlock
{
    qint16 min;
    qint16 max;
};

// 2^(i / 1024) for the compiler, without std::exp2 (not constexpr): the
// binary digits of i / 1024 pick the roots 2^(1/2), 2^(1/4) .. 2^(1/1024),
// each a square root of the one before (Newton's method)
struct TPeakCodeMath
{
    static constexpr double square_root(double x)   // x in [1, 2]
    {
        double root = x;
        for (int i = 0; i < 32; ++i) {
            const double next = 0.5 * (root + x / root);
            if (next == root) {
                break;
            }
            root = next;
        }
        return root;
    }

    // i in [0, 1024)
    static constexpr double octave_fraction(int i)
    {
        double value = 1.0;
        double root = 2.0;
        for (int bit = 9; bit >= 0; --bit) {
            root = square_root(root);
            if (i & (1 << bit)) {
                value *= root;
            }
        }
        return value;
    }
};

// The decoding tables of the value codes (TPeakLevels), built by the
// compiler. Code c > 0 is Octaves[c >> 10] * Mantissas[c & 1023]: octave
// c / 1024 and step c % 1024 within it. Code 0 is silence, -c is the value
// of c negated. Multiplying by a power of two is exact, so a value is as
// exact as its mantissa.
struct TPeakCodeTables
{
    static constexpr std::array<float, 1024> Mantissas = [] {
        std::array<float, 1024> mantissas{};
        for (int i = 0; i < 1024; ++i) {
            mantissas[size_t(i)] = float(TPeakCodeMath::octave_fraction(i));
        }
        return mantissas;
    }();

    // 2^(o - 28) for o = 0 .. 31
    static constexpr std::array<float, 32> Octaves = [] {
        std::array<float, 32> octaves{};
        float value = 1.0f;
        for (int o = 0; o < 28; ++o) {
            value *= 0.5f;
        }
        for (float& octave : octaves) {
            octave = value;
            value *= 2.0f;
        }
        return octaves;
    }();
};

/**
 * @brief Read-only view on a min/max and RMS pyramid of one audio source.
 *
 * Level 0 holds one block per BlockFrames frames of the source, at the
 * source's own sample rate. Every block has its min/max (TPeakBlock) and,
 * in an array of its own, the RMS of its frames as a code (nearest); a
 * parent's RMS is the root of its children's mean squares weighted by
 * their frames. Block k of level L covers the frames
 * [k * BlockFrames * 2^L, (k + 1) * BlockFrames * 2^L), and is the combination
 * of blocks 2k and 2k + 1 of level L - 1. The last block of a level may cover
 * fewer frames. A level may miss its last block while a recording grows (a
 * block is only added when both its children are complete).
 *
 * The blocks of a level live in chunks of 2^chunkShift blocks: one chunk
 * for a mapped file, many for a growing pyramid (which never moves a block).
 * The view does not own the data: TPeakPyramidBuilder (memory) and TPeakFile
 * (mapped file) do. A view taken from a growing pyramid is a snapshot: the
 * blocks it names never change, and its frame count covers them.
 */
class TPeakLevels
{
public:
    static constexpr int BlockFrames = 64;
    static constexpr int BlockFramesShift = 6;

    // Values (1.0 = full scale) are stored as int16 codes on a log scale:
    // code 0 is silence, code c > 0 is 2^((c - OneCode) / CodesPerOctave),
    // -c the same value negated. A step is 1/1024 octave (0.0059 dB) at any
    // level, from about -168 dB (code 1) to +24 dB (MaxCode); larger values
    // saturate. Codes compare like their values, so min and max work on
    // codes.
    static constexpr int CodesPerOctave = 1024;
    static constexpr int OneCode = 28 * CodesPerOctave;
    static constexpr int MaxCode = 32767;

    // A chunk's min/max and RMS codes, side by side so one lookup finds both
    struct Chunk
    {
        const TPeakBlock* blocks;
        const qint16* rms;
    };

    struct Level
    {
        qint64 blockCount{0};
        int chunkShift{62};            // 62: everything in one chunk
        qint64 chunkCount{0};          // per channel
        std::vector<Chunk> chunks;     // channel by channel: [channel * chunkCount + chunk]

        // Appends the next channel's chunks; every channel has as many
        void add_channel(const std::vector<const TPeakBlock*>& blocks, const std::vector<const qint16*>& rms)
        {
            Q_ASSERT(blocks.size() == rms.size() && (chunks.empty() || qint64(blocks.size()) == chunkCount));
            chunkCount = qint64(blocks.size());
            for (size_t i = 0; i < blocks.size(); ++i) {
                chunks.push_back({blocks[i], rms[i]});
            }
        }
        const Chunk& chunk(int channel, qint64 index) const { return chunks[size_t(channel * chunkCount + (index >> chunkShift))]; }
        qint64 offset(qint64 index) const { return index & ((qint64(1) << chunkShift) - 1); }
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
        return l.chunk(channel, index).blocks[l.offset(index)];
    }
    qint16 rms(int level, int channel, qint64 index) const
    {
        Q_ASSERT(index >= 0 && index < block_count(level));
        const Level& l = m_levels[size_t(level)];
        return l.chunk(channel, index).rms[l.offset(index)];
    }
    // The blocks (RMS codes) from index on that follow each other in memory:
    // their start and how many (at least 1 when index < block_count(level))
    const TPeakBlock* blocks(int level, int channel, qint64 index, qint64& count) const;
    const qint16* rms_blocks(int level, int channel, qint64 index, qint64& count) const;
    // The frames block index of level covers (the source's last one fewer)
    qint64 block_frames(int level, qint64 index) const
    {
        const qint64 start = index << (BlockFramesShift + level);
        return std::min(start + (qint64(BlockFrames) << level), m_frameCount) - start;
    }

    // The combined block of the level-0 blocks [first, last) of channel,
    // from the coarsest levels that fit. first < last <= block_count(0).
    // Reads min/max only.
    TPeakBlock range(int channel, qint64 first, qint64 last) const;
    // The same blocks' min/max and mean square (their RMS squared, weighted
    // by their frames)
    TPeakBlock range(int channel, qint64 first, qint64 last, double& meanSquare) const;

    static float to_value(qint16 code)
    {
        // Clamped: -32768 is never written, but a file may hold anything
        const int magnitude = std::min(code < 0 ? -int(code) : int(code), MaxCode);
        const float value = magnitude == 0 ? 0.0f
            : TPeakCodeTables::Octaves[size_t(magnitude >> 10)] * TPeakCodeTables::Mantissas[size_t(magnitude & 1023)];
        return code < 0 ? -value : value;
    }
    // The codes on both sides of value: the smallest code whose value is >=
    // value, the largest whose value is <= value (MaxCode or -MaxCode when
    // value is beyond the range: saturated). NaN gives 0.
    static qint16 code_at_or_above(float value);
    static qint16 code_at_or_below(float value);
    // The code whose value is nearest (RMS: no side to keep)
    static qint16 code_nearest(float value);
    static float max_value() { return to_value(MaxCode); }

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
        std::vector<std::unique_ptr<qint16[]>> rmsChunks;           // the same chunks' RMS codes
        // Sized once, read by snapshots. Atomic entries: release_periods()
        // clears the entries of released chunks while a snapshot may read them
        std::vector<std::unique_ptr<std::atomic<const TPeakBlock*>[]>> pages;
        std::vector<std::unique_ptr<std::atomic<const qint16*>[]>> rmsPages;
        qint64 releasedChunks{0};
        qint64 count{0};
        std::atomic<qint64> publishedCount{0};
        // The last block while it has no pair yet: its mean square before
        // rounding and its frames, so a parent is made from exact values
        double pendingMeanSquare{0.0};
        qint64 pendingFrames{0};

        const TPeakBlock& at(qint64 index) const
        {
            return chunks[size_t(index >> chunkShift)][index & ((qint64(1) << chunkShift) - 1)];
        }
        const TPeakBlock* published_chunk(qint64 chunk) const
        {
            return pages[size_t(chunk >> PageShift)][chunk & (PageSize - 1)].load(std::memory_order_relaxed);
        }
        const qint16* published_rms_chunk(qint64 chunk) const
        {
            return rmsPages[size_t(chunk >> PageShift)][chunk & (PageSize - 1)].load(std::memory_order_relaxed);
        }
    };

    struct Channel
    {
        float min;
        float max;
        float sumSquares;
        std::vector<LevelStore> levels;   // MaxLevels, so nothing ever resizes
    };

    void add_block(Channel& channel, int level, TPeakBlock block, double meanSquare, qint64 frames);
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
