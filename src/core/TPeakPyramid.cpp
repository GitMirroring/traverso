// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TPeakPyramid.h"

#include <algorithm>
#include <cmath>
#include <limits>

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

    TPeakBlock result{qint16(MaxValue), qint16(-MaxValue)};
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

qint16 TPeakLevels::quantize_min(float value)
{
    if (std::isnan(value)) {
        return 0;
    }
    const float scaled = std::floor(value * ValueScale);
    return qint16(std::clamp(scaled, float(-MaxValue), float(MaxValue)));
}

qint16 TPeakLevels::quantize_max(float value)
{
    if (std::isnan(value)) {
        return 0;
    }
    const float scaled = std::ceil(value * ValueScale);
    return qint16(std::clamp(scaled, float(-MaxValue), float(MaxValue)));
}


TPeakPyramidBuilder::TPeakPyramidBuilder(uint rate, int channelCount)
    : m_rate(rate)
    , m_channels(size_t(channelCount))
{
    Q_ASSERT(rate > 0 && channelCount > 0);
    for (Channel& channel : m_channels) {
        channel.min = std::numeric_limits<float>::max();
        channel.max = std::numeric_limits<float>::lowest();
        channel.levels = std::vector<LevelStore>(MaxLevels);
        for (int level = 0; level < MaxLevels; ++level) {
            const qint64 maxBlocks = std::max<qint64>(1, MaxLevel0Blocks >> level);
            const qint64 maxChunks = (maxBlocks + (qint64(1) << ChunkShift) - 1) >> ChunkShift;
            channel.levels[size_t(level)].published.assign(size_t(maxChunks), nullptr);
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
        TPeakLevels::Level snapshot;
        snapshot.blockCount = count;
        snapshot.chunkShift = ChunkShift;
        const size_t chunks = size_t((count - 1) >> ChunkShift) + 1;
        for (const Channel& channel : m_channels) {
            const std::vector<const TPeakBlock*>& published = channel.levels[size_t(level)].published;
            snapshot.channels.emplace_back(published.begin(), published.begin() + qsizetype(chunks));
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
    const qint64 chunkIndex = store.count >> ChunkShift;
    const qint64 inChunk = store.count & ((qint64(1) << ChunkShift) - 1);

    if (inChunk == 0) {
        if (size_t(chunkIndex) >= store.published.size()) {
            m_full = true;   // longer than MaxLevel0Blocks: the pyramid stops here
            return;
        }
        store.chunks.push_back(std::make_unique<TPeakBlock[]>(size_t(1) << ChunkShift));
        store.published[size_t(chunkIndex)] = store.chunks.back().get();
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
        add_block(channel, 0, {TPeakLevels::quantize_min(channel.min), TPeakLevels::quantize_max(channel.max)});
        channel.min = std::numeric_limits<float>::max();
        channel.max = std::numeric_limits<float>::lowest();
    }

    m_blockFill = 0;
}
