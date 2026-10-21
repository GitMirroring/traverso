// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QtGlobal>

#include <algorithm>
#include <array>
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

    // 2^(1/2), 2^(1/4) .. 2^(1/1024)
    static constexpr std::array<double, 10> octave_roots()
    {
        std::array<double, 10> roots{};
        double root = 2.0;
        for (double& r : roots) {
            root = square_root(root);
            r = root;
        }
        return roots;
    }

    // i in [0, 1024), from the roots (computed once for a table)
    static constexpr double octave_fraction(int i, const std::array<double, 10>& roots)
    {
        double value = 1.0;
        for (int bit = 9; bit >= 0; --bit) {
            if (i & (1 << bit)) {
                value *= roots[size_t(9 - bit)];
            }
        }
        return value;
    }
    static constexpr double octave_fraction(int i) { return octave_fraction(i, octave_roots()); }
};

// The decoding tables of the value codes (TPeakLevels), built by the
// compiler. Code c > 0 is Octaves[c >> 10] * Mantissas[c & 1023]: octave
// c / 1024 and step c % 1024 within it. Code 0 is silence, -c is the value
// of c negated. Multiplying by a power of two is exact, so a value is as
// exact as its mantissa.
struct TPeakCodeTables
{
    static constexpr std::array<double, 10> Roots = TPeakCodeMath::octave_roots();

    static constexpr std::array<float, 1024> Mantissas = [] {
        std::array<float, 1024> mantissas{};
        for (int i = 0; i < 1024; ++i) {
            mantissas[size_t(i)] = float(TPeakCodeMath::octave_fraction(i, Roots));
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
 * source's own sample rate. Every block has its min/max (TPeakBlock) and the
 * RMS of its frames as a code (nearest; a level-0 block with a NaN or an inf
 * sample gets its peak instead); a parent's RMS is the root of its children's
 * mean squares weighted by their frames. Block k of level L covers the frames
 * [k * BlockFrames * 2^L, (k + 1) * BlockFrames * 2^L), and is the
 * combination of blocks 2k and 2k + 1 of level L - 1. The last block of a
 * level may cover fewer frames; a level has half the blocks of the one below,
 * rounded up, up to a level of one block.
 *
 * Every level is an array of records, one per block: the min and max code of
 * every channel, then the RMS code of every channel (record_codes()). The view
 * does not own them: TPeakFile (a mapped file) and TPeakStream (the level-0
 * records of a file being written, copied) do. A level may hold a window of
 * its blocks only, [first, end): a view of a file being written.
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

    // The codes of a record: min and max of every channel, then the RMS of
    // every channel
    static constexpr int record_codes(int channelCount) { return 3 * channelCount; }

    struct Level
    {
        const qint16* records{nullptr};   // the records of the blocks [first, end)
        qint64 first{0};
        qint64 end{0};                    // the level's blocks end here
    };

    TPeakLevels() = default;
    TPeakLevels(uint rate, int channelCount, qint64 frameCount, std::vector<Level> levels);

    uint rate() const { return m_rate; }
    int channel_count() const { return m_channelCount; }
    qint64 frame_count() const { return m_frameCount; }

    // 0 when there is no pyramid (yet); frame_count() and rate() still tell
    // about the source then
    int level_count() const { return int(m_levels.size()); }
    // Where the level's blocks end: its block count (for a window, the
    // blocks it reaches)
    qint64 block_count(int level) const { return m_levels[size_t(level)].end; }
    TPeakBlock block(int level, int channel, qint64 index) const
    {
        const qint16* codes = record(level, index);
        return {codes[2 * channel], codes[2 * channel + 1]};
    }
    qint16 rms(int level, int channel, qint64 index) const { return record(level, index)[2 * m_channelCount + channel]; }
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

    // The value of a code's magnitude, 0 .. MaxCode
    static float magnitude_value(int magnitude)
    {
        return magnitude == 0 ? 0.0f
            : TPeakCodeTables::Octaves[size_t(magnitude >> 10)] * TPeakCodeTables::Mantissas[size_t(magnitude & 1023)];
    }
    static float to_value(qint16 code)
    {
        // Clamped: -32768 is never written, but a file may hold anything
        const float value = magnitude_value(std::min(code < 0 ? -int(code) : int(code), MaxCode));
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
    const qint16* record(int level, qint64 index) const
    {
        const Level& l = m_levels[size_t(level)];
        Q_ASSERT(index >= l.first && index < l.end);
        return l.records + (index - l.first) * record_codes(m_channelCount);
    }

    uint m_rate{0};
    int m_channelCount{0};
    qint64 m_frameCount{0};
    std::vector<Level> m_levels;
};
