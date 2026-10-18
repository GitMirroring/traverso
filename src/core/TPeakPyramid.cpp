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

// The smallest positive code whose value is >= magnitude (> 0)
static int magnitude_at_or_above(float magnitude)
{
    if (!(magnitude <= TPeakLevels::max_value())) {
        return TPeakLevels::MaxCode;
    }
    int exponent = 0;
    const float mantissa = 2.0f * std::frexp(magnitude, &exponent);   // [1, 2)
    int octave = exponent - 1 + (TPeakLevels::OneCode >> 10);
    if (octave < 0) {
        return 1;
    }
    const auto& mantissas = TPeakCodeTables::Mantissas;
    int index = int(std::lower_bound(mantissas.begin(), mantissas.end(), mantissa) - mantissas.begin());
    if (index == TPeakLevels::CodesPerOctave) {
        ++octave;
        index = 0;
    }
    return std::clamp(octave * TPeakLevels::CodesPerOctave + index, 1, TPeakLevels::MaxCode);
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
    int exponent = 0;
    const float mantissa = 2.0f * std::frexp(magnitude, &exponent);
    const int octave = exponent - 1 + (TPeakLevels::OneCode >> 10);
    if (octave < 0) {
        return 0;
    }
    const auto& mantissas = TPeakCodeTables::Mantissas;
    const int index = int(std::upper_bound(mantissas.begin(), mantissas.end(), mantissa) - mantissas.begin()) - 1;
    return octave * TPeakLevels::CodesPerOctave + index;
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
    return l.channels[size_t(channel)][size_t(index >> l.chunkShift)] + inChunk;
}

TPeakBlock TPeakLevels::range(int channel, qint64 first, qint64 last) const
{
    Q_ASSERT(level_count() > 0);
    Q_ASSERT(first >= 0 && first < last && last <= block_count(0));

    TPeakBlock result{qint16(MaxCode), qint16(-MaxCode)};
    int level = 0;

    // Like a segment tree: take the blocks at the odd ends of the range, then
    // continue one level up with the pairs that are fully inside it
    while (first < last) {
        if (level + 1 >= level_count()) {
            for (qint64 index = first; index < last; ++index) {
                combine(result, block(level, channel, index));
            }
            break;
        }
        if (first & 1) {
            combine(result, block(level, channel, first));
            ++first;
        }
        if (last & 1) {
            --last;
            combine(result, block(level, channel, last));
        }
        if (first >= last) {
            break;
        }
        // While a recording grows, the next level can miss its last block
        const qint64 split = std::clamp(block_count(level + 1) * 2, first, last);
        for (qint64 index = split; index < last; ++index) {
            combine(result, block(level, channel, index));
        }
        last = split;

        first /= 2;
        last /= 2;
        ++level;
    }

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
    const qint16 below = code_at_or_below(value);
    const qint16 above = code_at_or_above(value);
    return value - to_value(below) <= to_value(above) - value ? below : above;
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
        channel.levels = std::vector<LevelStore>(MaxLevels);
        for (int level = 0; level < MaxLevels; ++level) {
            LevelStore& store = channel.levels[size_t(level)];
            // A chunk holds whole periods of this level: 2^(periodShift - level)
            // blocks per period, but never less than MinChunkShift
            store.chunkShift = std::max(periodShift - level, MinChunkShift);
            const qint64 maxBlocks = std::max<qint64>(1, MaxLevel0Blocks >> level);
            const qint64 maxChunks = (maxBlocks + (qint64(1) << store.chunkShift) - 1) >> store.chunkShift;
            store.pages = std::vector<std::unique_ptr<std::atomic<const TPeakBlock*>[]>>(size_t((maxChunks + PageSize - 1) >> PageShift));
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
#ifndef NDEBUG
                // A read of released blocks shows up as nonsense, not as the
                // old values that freed memory often still holds
                std::fill_n(store.chunks[size_t(store.releasedChunks)].get(), size_t(1) << store.chunkShift,
                            TPeakBlock{qint16(0x5a5a), qint16(-0x5a5a)});
#endif
                store.chunks[size_t(store.releasedChunks)].reset();
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
            for (int i = 0; i < count; ++i) {
                low = std::min(low, samples[i]);
                high = std::max(high, samples[i]);
            }
            m_channels[c].min = low;
            m_channels[c].max = high;
        }

        position += count;
        m_blockFill += count;
        if (m_blockFill == TPeakLevels::BlockFrames) {
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
                add_block(channel, level + 1, store.at(store.count - 1));
            }
        }
    }
}

TPeakLevels TPeakPyramidBuilder::levels() const
{
    const qint64 frames = m_publishedFrames.load(std::memory_order_acquire);
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
            for (qint64 chunk = 0; chunk < chunks; ++chunk) {
                pointers[size_t(chunk)] = store.published_chunk(chunk);
            }
            snapshot.channels.push_back(std::move(pointers));
        }
        levels.push_back(std::move(snapshot));
    }

    return TPeakLevels(m_rate, int(m_channels.size()), frames, std::move(levels));
}

void TPeakPyramidBuilder::add_block(Channel& channel, int level, TPeakBlock block)
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
        }
        // Not zeroed: a block is written before its count is published
        store.chunks.emplace_back(new TPeakBlock[size_t(1) << store.chunkShift]);
        store.pages[page][chunkIndex & (PageSize - 1)].store(store.chunks.back().get(), std::memory_order_relaxed);
    }

    store.chunks[size_t(chunkIndex)][inChunk] = block;
    ++store.count;
    store.publishedCount.store(store.count, std::memory_order_release);

    if ((store.count & 1) == 0) {
        TPeakBlock pair = store.at(store.count - 2);
        combine(pair, block);
        add_block(channel, level + 1, pair);
    }
}

void TPeakPyramidBuilder::close_partial_block()
{
    if (m_blockFill == 0) {
        return;
    }

    for (Channel& channel : m_channels) {
        add_block(channel, 0, {TPeakLevels::code_at_or_below(channel.min), TPeakLevels::code_at_or_above(channel.max)});
        channel.min = std::numeric_limits<float>::max();
        channel.max = std::numeric_limits<float>::lowest();
    }

    m_blockFill = 0;
}
