// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QtGlobal>

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
 * The view does not own the data: TPeakPyramidBuilder (memory) and TPeakFile
 * (mapped file) do.
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
        std::vector<const TPeakBlock*> channels;   // blockCount blocks each
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
        return m_levels[size_t(level)].channels[size_t(channel)][index];
    }

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
 */
class TPeakPyramidBuilder
{
public:
    TPeakPyramidBuilder(uint rate, int channelCount);

    // channels[c] points to frames samples of channel c
    void append(const float* const* channels, qint64 frames);
    void finish();

    bool is_finished() const { return m_finished; }
    qint64 frame_count() const { return m_frameCount; }

    // Valid until the next append() or finish()
    TPeakLevels levels() const;

private:
    struct Channel
    {
        float min;
        float max;
        std::vector<std::vector<TPeakBlock>> levels;
    };

    void add_block(Channel& channel, int level, TPeakBlock block);
    void close_partial_block();

    uint m_rate;
    std::vector<Channel> m_channels;
    qint64 m_frameCount{0};
    int m_blockFill{0};   // frames in the current level-0 block
    bool m_finished{false};
};
