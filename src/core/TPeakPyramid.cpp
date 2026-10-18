// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TPeakPyramid.h"

#include <algorithm>
#include <cmath>
#include <limits>

// The tables are what the codes mean: check them where they are built
static constexpr bool mantissas_increase()
{
    for (size_t j = 1; j < TPeakCodeTables::Mantissas.size(); ++j) {
        if (!(TPeakCodeTables::Mantissas[j - 1] < TPeakCodeTables::Mantissas[j])) {
            return false;
        }
    }
    return true;
}
// 1024 steps of 2^(1/1024) make an octave
static constexpr double octave_of_steps()
{
    double value = TPeakCodeMath::octave_fraction(1);
    for (int i = 0; i < 10; ++i) {
        value *= value;
    }
    return value;
}
static_assert(octave_of_steps() > 2.0 - 1e-12 && octave_of_steps() < 2.0 + 1e-12, "octave_fraction(1) is not 2^(1/1024)");
static_assert(TPeakCodeMath::octave_fraction(512) * TPeakCodeMath::octave_fraction(512) > 2.0 - 1e-15
              && TPeakCodeMath::octave_fraction(512) * TPeakCodeMath::octave_fraction(512) < 2.0 + 1e-15);
static_assert(TPeakCodeTables::Mantissas[0] == 1.0f && TPeakCodeTables::Mantissas[1023] < 2.0f && mantissas_increase());
static_assert(TPeakCodeTables::Octaves[TPeakLevels::OneCode >> 10] == 1.0f && TPeakCodeTables::Octaves[31] == 8.0f);
static_assert((TPeakLevels::MaxCode >> 10) == 31 && TPeakLevels::CodesPerOctave == 1024);

// The index of the first mantissa >= mantissa (upper: > mantissa), 0 ..
// 1024: a binary search without branches in the loop, so its speed does not
// depend on the branch predictor (the builder encodes several values per
// block)
static int mantissa_index(float mantissa, bool upper)
{
    const auto& mantissas = TPeakCodeTables::Mantissas;
    int base = 0;
    for (int step = TPeakLevels::CodesPerOctave / 2; step > 0; step >>= 1) {
        const float probe = mantissas[size_t(base + step)];
        base += (upper ? probe <= mantissa : probe < mantissa) ? step : 0;
    }
    const float last = mantissas[size_t(base)];
    return base + ((upper ? last <= mantissa : last < mantissa) ? 1 : 0);
}

// A positive magnitude below the top of the code range as its octave (of
// the codes, may be negative below the range) and its mantissa in [1, 2)
static int split(float magnitude, float& mantissa)
{
    int exponent = 0;
    mantissa = 2.0f * std::frexp(magnitude, &exponent);
    return exponent - 1 + (TPeakLevels::OneCode >> 10);
}

// The smallest positive code whose value is >= magnitude (> 0)
static int magnitude_at_or_above(float magnitude)
{
    if (!(magnitude <= TPeakLevels::max_value())) {
        return TPeakLevels::MaxCode;
    }
    float mantissa = 0.0f;
    const int octave = split(magnitude, mantissa);
    if (octave < 0) {
        return 1;
    }
    // An index of 1024 is the next octave's first code
    return std::clamp(octave * TPeakLevels::CodesPerOctave + mantissa_index(mantissa, false), 1, TPeakLevels::MaxCode);
}

// The largest code whose value is <= magnitude (>= 0)
static int magnitude_at_or_below(float magnitude)
{
    if (!(magnitude > 0.0f)) {
        return 0;
    }
    if (magnitude >= TPeakLevels::max_value()) {
        return TPeakLevels::MaxCode;
    }
    float mantissa = 0.0f;
    const int octave = split(magnitude, mantissa);
    if (octave < 0) {
        return 0;
    }
    // At least 1: the first mantissa is 1.0, mantissa is not below it
    return octave * TPeakLevels::CodesPerOctave + mantissa_index(mantissa, true) - 1;
}

// The code whose value is nearest to magnitude (>= 0): one search, then the
// nearer of the two neighbours
static int magnitude_nearest(float magnitude)
{
    if (!(magnitude > 0.0f)) {
        return 0;
    }
    if (magnitude >= TPeakLevels::max_value()) {
        return TPeakLevels::MaxCode;
    }
    float mantissa = 0.0f;
    const int octave = split(magnitude, mantissa);
    if (octave < 0) {
        return magnitude > TPeakLevels::to_value(1) * 0.5f ? 1 : 0;
    }
    const auto& mantissas = TPeakCodeTables::Mantissas;
    const int above = mantissa_index(mantissa, false);   // >= 1: mantissa >= 1.0 = the first
    const float high = above == TPeakLevels::CodesPerOctave ? 2.0f : mantissas[size_t(above)];
    const float low = mantissas[size_t(std::max(above - 1, 0))];
    const int index = (above > 0 && mantissa - low <= high - mantissa) ? above - 1 : above;
    return std::clamp(octave * TPeakLevels::CodesPerOctave + index, 0, TPeakLevels::MaxCode);
}

static void combine(TPeakBlock& into, const TPeakBlock& block)
{
    into.min = std::min(into.min, block.min);
    into.max = std::max(into.max, block.max);
}

TPeakLevels::TPeakLevels(uint rate, int channelCount, qint64 frameCount, std::vector<Level> levels)
    : m_rate(rate)
    , m_channelCount(channelCount)
    , m_frameCount(frameCount)
    , m_levels(std::move(levels))
{
}

const TPeakBlock* TPeakLevels::blocks(int level, int channel, qint64 index, qint64& count) const
{
    Q_ASSERT(index >= 0 && index < block_count(level));
    const Level& l = m_levels[size_t(level)];
    const qint64 chunkBlocks = qint64(1) << l.chunkShift;
    const qint64 inChunk = index & (chunkBlocks - 1);
    count = std::min(chunkBlocks - inChunk, l.blockCount - index);
    return l.chunk(channel, index).blocks + inChunk;
}

const qint16* TPeakLevels::rms_blocks(int level, int channel, qint64 index, qint64& count) const
{
    Q_ASSERT(index >= 0 && index < block_count(level));
    const Level& l = m_levels[size_t(level)];
    const qint64 chunkBlocks = qint64(1) << l.chunkShift;
    const qint64 inChunk = index & (chunkBlocks - 1);
    count = std::min(chunkBlocks - inChunk, l.blockCount - index);
    return l.chunk(channel, index).rms + inChunk;
}

// The blocks that make up the level-0 blocks [first, last), from the
// coarsest levels that fit: visit(level, index) for each. Like a segment
// tree: take the blocks at the odd ends of the range, then continue one
// level up with the pairs that are fully inside it.
template <typename Visit>
static void visit_range(const TPeakLevels& levels, qint64 first, qint64 last, Visit&& visit)
{
    Q_ASSERT(levels.level_count() > 0);
    Q_ASSERT(first >= 0 && first < last && last <= levels.block_count(0));

    int level = 0;
    while (first < last) {
        if (level + 1 >= levels.level_count()) {
            for (qint64 index = first; index < last; ++index) {
                visit(level, index);
            }
            break;
        }
        if (first & 1) {
            visit(level, first);
            ++first;
        }
        if (last & 1) {
            --last;
            visit(level, last);
        }
        if (first >= last) {
            break;
        }
        // While a recording grows, the next level can miss its last block
        const qint64 split = std::clamp(levels.block_count(level + 1) * 2, first, last);
        for (qint64 index = split; index < last; ++index) {
            visit(level, index);
        }
        last = split;

        first /= 2;
        last /= 2;
        ++level;
    }
}

TPeakBlock TPeakLevels::range(int channel, qint64 first, qint64 last) const
{
    TPeakBlock result{qint16(MaxCode), qint16(-MaxCode)};
    visit_range(*this, first, last, [&](int level, qint64 index) { combine(result, block(level, channel, index)); });
    return result;
}

TPeakBlock TPeakLevels::range(int channel, qint64 first, qint64 last, double& meanSquare) const
{
    TPeakBlock result{qint16(MaxCode), qint16(-MaxCode)};
    double sum = 0.0;
    visit_range(*this, first, last, [&](int level, qint64 index) {
        // The block and its RMS from one chunk lookup; an RMS code is >= 0
        const Level& l = m_levels[size_t(level)];
        const Chunk& chunk = l.chunk(channel, index);
        const qint64 offset = l.offset(index);
        combine(result, chunk.blocks[offset]);
        const qint16 code = chunk.rms[offset];
        const float value = code > 0 ? TPeakCodeTables::Octaves[size_t(code >> 10)] * TPeakCodeTables::Mantissas[size_t(code & 1023)] : 0.0f;
        sum += double(value * value) * double(block_frames(level, index));
    });
    // The blocks cover these frames together
    const qint64 frames = std::min(last << BlockFramesShift, m_frameCount) - (first << BlockFramesShift);
    meanSquare = frames > 0 ? sum / double(frames) : 0.0;
    return result;
}

qint16 TPeakLevels::code_at_or_above(float value)
{
    if (value > 0.0f) {
        return qint16(magnitude_at_or_above(value));
    }
    if (value < 0.0f) {
        return qint16(-magnitude_at_or_below(-value));
    }
    return 0;   // zero or NaN
}

qint16 TPeakLevels::code_at_or_below(float value)
{
    return qint16(-code_at_or_above(-value));
}

qint16 TPeakLevels::code_nearest(float value)
{
    return qint16(value < 0.0f ? -magnitude_nearest(-value) : magnitude_nearest(value));
}


TPeakPyramidBuilder::TPeakPyramidBuilder(uint rate, int channelCount, int periodShift)
    : m_rate(rate)
    , m_periodShift(periodShift)
    , m_channels(size_t(channelCount))
{
    Q_ASSERT(rate > 0 && channelCount > 0);
    Q_ASSERT(periodShift >= 0 && periodShift <= MaxPeriodShift);
    for (Channel& channel : m_channels) {
        channel.min = std::numeric_limits<float>::max();
        channel.max = std::numeric_limits<float>::lowest();
        channel.sumSquares = 0.0f;
        channel.levels = std::vector<LevelStore>(MaxLevels);
        for (int level = 0; level < MaxLevels; ++level) {
            LevelStore& store = channel.levels[size_t(level)];
            // A chunk holds whole periods of this level: 2^(periodShift - level)
            // blocks per period, but never less than MinChunkShift
            store.chunkShift = std::max(periodShift - level, MinChunkShift);
            const qint64 maxBlocks = std::max<qint64>(1, MaxLevel0Blocks >> level);
            const qint64 maxChunks = (maxBlocks + (qint64(1) << store.chunkShift) - 1) >> store.chunkShift;
            store.pages = std::vector<std::unique_ptr<std::atomic<const TPeakBlock*>[]>>(size_t((maxChunks + PageSize - 1) >> PageShift));
            store.rmsPages = std::vector<std::unique_ptr<std::atomic<const qint16*>[]>>(store.pages.size());
        }
    }
}

qint64 TPeakPyramidBuilder::complete_periods() const
{
    // A level's part of a period is complete when the level has all its
    // blocks; after finish() the promoted last blocks can complete a period
    // of a higher level only, so take the minimum
    qint64 periods = std::numeric_limits<qint64>::max();
    for (const Channel& channel : m_channels) {
        for (int level = 0; level <= m_periodShift && level < MaxLevels; ++level) {
            periods = std::min(periods, channel.levels[size_t(level)].count >> (m_periodShift - level));
        }
    }
    return periods;
}

qint64 TPeakPyramidBuilder::allocated_blocks() const
{
    qint64 blocks = 0;
    for (const Channel& channel : m_channels) {
        for (const LevelStore& store : channel.levels) {
            blocks += (qint64(store.chunks.size()) - store.releasedChunks) << store.chunkShift;
        }
    }
    return blocks;
}

void TPeakPyramidBuilder::release_periods(qint64 periods)
{
    // add_block() never reads a block of an earlier chunk (chunks hold an
    // even number of blocks), so released chunks are not touched again
    for (Channel& channel : m_channels) {
        for (int level = 0; level <= m_periodShift && level < MaxLevels; ++level) {
            LevelStore& store = channel.levels[size_t(level)];
            const qint64 releasedBlocks = periods << (m_periodShift - level);
            const qint64 chunks = std::min<qint64>(releasedBlocks >> store.chunkShift, qint64(store.chunks.size()));
            for (; store.releasedChunks < chunks; ++store.releasedChunks) {
                store.pages[size_t(store.releasedChunks >> PageShift)][store.releasedChunks & (PageSize - 1)].store(nullptr, std::memory_order_relaxed);
                store.rmsPages[size_t(store.releasedChunks >> PageShift)][store.releasedChunks & (PageSize - 1)].store(nullptr, std::memory_order_relaxed);
#ifndef NDEBUG
                // A read of released blocks shows up as nonsense, not as the
                // old values that freed memory often still holds
                std::fill_n(store.chunks[size_t(store.releasedChunks)].get(), size_t(1) << store.chunkShift,
                            TPeakBlock{qint16(0x5a5a), qint16(-0x5a5a)});
                std::fill_n(store.rmsChunks[size_t(store.releasedChunks)].get(), size_t(1) << store.chunkShift, qint16(0x5a5a));
#endif
                store.chunks[size_t(store.releasedChunks)].reset();
                store.rmsChunks[size_t(store.releasedChunks)].reset();
            }
        }
    }
}

void TPeakPyramidBuilder::append(const float* const* channels, qint64 frames)
{
    Q_ASSERT(!m_finished);
    if (m_full) {
        return;
    }

    qint64 position = 0;
    while (position < frames) {
        const int count = int(std::min<qint64>(frames - position, TPeakLevels::BlockFrames - m_blockFill));

        for (size_t c = 0; c < m_channels.size(); ++c) {
            const float* samples = channels[c] + position;
            float low = m_channels[c].min;
            float high = m_channels[c].max;
            float squares = m_channels[c].sumSquares;
            for (int i = 0; i < count; ++i) {
                low = std::min(low, samples[i]);
                high = std::max(high, samples[i]);
                squares += samples[i] * samples[i];
            }
            m_channels[c].min = low;
            m_channels[c].max = high;
            m_channels[c].sumSquares = squares;
        }

        position += count;
        m_blockFill += count;
        if (m_blockFill == TPeakLevels::BlockFrames) {
            // The frames first: a snapshot's frame count covers the blocks it
            // names (levels() reads the counts before the frames)
            m_publishedFrames.store(m_frameCount + position, std::memory_order_release);
            close_partial_block();
            if (m_full) {
                break;
            }
        }
    }

    m_frameCount += position;
    m_publishedFrames.store(m_frameCount, std::memory_order_release);
}

void TPeakPyramidBuilder::finish()
{
    if (m_finished) {
        return;
    }
    m_finished = true;

    close_partial_block();

    // A level with an odd number of blocks has a last block without a pair:
    // it covers the end of the source alone, so it moves up as it is
    for (Channel& channel : m_channels) {
        for (int level = 0; level + 1 < MaxLevels; ++level) {
            const LevelStore& store = channel.levels[size_t(level)];
            if (store.count > 1 && (store.count & 1)) {
                add_block(channel, level + 1, store.at(store.count - 1), store.pendingMeanSquare, store.pendingFrames);
            }
        }
    }
}

TPeakLevels TPeakPyramidBuilder::levels() const
{
    std::vector<TPeakLevels::Level> levels;

    for (int level = 0; level < MaxLevels; ++level) {
        // The channels are appended one after the other, so a snapshot takes
        // what every channel has
        qint64 count = std::numeric_limits<qint64>::max();
        for (const Channel& channel : m_channels) {
            count = std::min(count, channel.levels[size_t(level)].publishedCount.load(std::memory_order_acquire));
        }
        if (count == 0) {
            break;
        }
        const int chunkShift = m_channels.front().levels[size_t(level)].chunkShift;
        TPeakLevels::Level snapshot;
        snapshot.blockCount = count;
        snapshot.chunkShift = chunkShift;
        const qint64 chunks = ((count - 1) >> chunkShift) + 1;
        for (const Channel& channel : m_channels) {
            const LevelStore& store = channel.levels[size_t(level)];
            std::vector<const TPeakBlock*> pointers(static_cast<size_t>(chunks));
            std::vector<const qint16*> rms(static_cast<size_t>(chunks));
            for (qint64 chunk = 0; chunk < chunks; ++chunk) {
                pointers[size_t(chunk)] = store.published_chunk(chunk);
                rms[size_t(chunk)] = store.published_rms_chunk(chunk);
            }
            snapshot.add_channel(pointers, rms);
        }
        levels.push_back(std::move(snapshot));
    }

    // After the counts: the frames published before them cover their blocks
    const qint64 frames = m_publishedFrames.load(std::memory_order_acquire);
    return TPeakLevels(m_rate, int(m_channels.size()), frames, std::move(levels));
}

void TPeakPyramidBuilder::add_block(Channel& channel, int level, TPeakBlock block, double meanSquare, qint64 frames)
{
    if (level >= MaxLevels) {
        return;
    }
    LevelStore& store = channel.levels[size_t(level)];
    const qint64 chunkIndex = store.count >> store.chunkShift;
    const qint64 inChunk = store.count & ((qint64(1) << store.chunkShift) - 1);

    if (inChunk == 0) {
        const size_t page = size_t(chunkIndex >> PageShift);
        if (page >= store.pages.size()) {
            m_full = true;   // longer than MaxLevel0Blocks: the pyramid stops here
            return;
        }
        if (!store.pages[page]) {
            store.pages[page] = std::make_unique<std::atomic<const TPeakBlock*>[]>(size_t(PageSize));
            store.rmsPages[page] = std::make_unique<std::atomic<const qint16*>[]>(size_t(PageSize));
        }
        // Not zeroed: a block is written before its count is published
        store.chunks.emplace_back(new TPeakBlock[size_t(1) << store.chunkShift]);
        store.rmsChunks.emplace_back(new qint16[size_t(1) << store.chunkShift]);
        store.pages[page][chunkIndex & (PageSize - 1)].store(store.chunks.back().get(), std::memory_order_relaxed);
        store.rmsPages[page][chunkIndex & (PageSize - 1)].store(store.rmsChunks.back().get(), std::memory_order_relaxed);
    }

    store.chunks[size_t(chunkIndex)][inChunk] = block;
    store.rmsChunks[size_t(chunkIndex)][inChunk] = TPeakLevels::code_nearest(float(std::sqrt(meanSquare)));
    ++store.count;
    store.publishedCount.store(store.count, std::memory_order_release);

    if ((store.count & 1) == 0) {
        // The parent from the exact mean squares, not from the rounded codes,
        // so every stored RMS is rounded once
        TPeakBlock pair = store.at(store.count - 2);
        combine(pair, block);
        const qint64 pairFrames = store.pendingFrames + frames;
        const double pairMeanSquare = (store.pendingMeanSquare * double(store.pendingFrames) + meanSquare * double(frames)) / double(pairFrames);
        add_block(channel, level + 1, pair, pairMeanSquare, pairFrames);
    } else {
        store.pendingMeanSquare = meanSquare;
        store.pendingFrames = frames;
    }
}

void TPeakPyramidBuilder::close_partial_block()
{
    if (m_blockFill == 0) {
        return;
    }

    // A partial last block weighs its own frames
    for (Channel& channel : m_channels) {
        add_block(channel, 0, {TPeakLevels::code_at_or_below(channel.min), TPeakLevels::code_at_or_above(channel.max)},
                  double(channel.sumSquares) / double(m_blockFill), m_blockFill);
        channel.min = std::numeric_limits<float>::max();
        channel.max = std::numeric_limits<float>::lowest();
        channel.sumSquares = 0.0f;
    }

    m_blockFill = 0;
}
