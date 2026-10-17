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
    }
}

void TPeakPyramidBuilder::append(const float* const* channels, qint64 frames)
{
    Q_ASSERT(!m_finished);

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
        }
    }

    m_frameCount += frames;
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
        for (size_t level = 0; level < channel.levels.size(); ++level) {
            const std::vector<TPeakBlock>& blocks = channel.levels[level];
            if (blocks.size() > 1 && (blocks.size() & 1)) {
                add_block(channel, int(level) + 1, blocks.back());
            }
        }
    }
}

TPeakLevels TPeakPyramidBuilder::levels() const
{
    std::vector<TPeakLevels::Level> levels(m_channels.front().levels.size());

    for (size_t level = 0; level < levels.size(); ++level) {
        levels[level].blockCount = qint64(m_channels.front().levels[level].size());
        for (const Channel& channel : m_channels) {
            levels[level].channels.push_back(channel.levels[level].data());
        }
    }

    return TPeakLevels(m_rate, int(m_channels.size()), m_frameCount, std::move(levels));
}

void TPeakPyramidBuilder::add_block(Channel& channel, int level, TPeakBlock block)
{
    if (int(channel.levels.size()) <= level) {
        channel.levels.resize(size_t(level) + 1);
    }

    std::vector<TPeakBlock>& blocks = channel.levels[size_t(level)];
    blocks.push_back(block);

    if ((blocks.size() & 1) == 0) {
        TPeakBlock pair = blocks[blocks.size() - 2];
        combine(pair, blocks.back());
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
