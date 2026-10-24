// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TPeakPyramid.h"

#include <algorithm>
#include <cmath>

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
    // Code 0 is silence, not 2^-28: at the bottom octave's first step the
    // nearest code is 1
    return std::clamp(octave * TPeakLevels::CodesPerOctave + index, 1, TPeakLevels::MaxCode);
}

static void combine(TPeakBlock& into, const TPeakBlock& block)
{
    into.min = std::min(into.min, block.min);
    into.max = std::max(into.max, block.max);
}

TPeakLevels::TPeakLevels(uint rate, int channelCount, qint64 frameCount, int chunkLevel, const qint16* chunks, qint64 chunkCount,
                         const qint16* tail)
    : m_rate(rate)
    , m_channelCount(channelCount)
    , m_frameCount(frameCount)
    , m_chunkLevel(chunkLevel)
    , m_chunks(chunks)
    , m_chunkCount(chunkCount)
    , m_tail(tail)
{
    Q_ASSERT(chunkLevel > 0 && chunkLevel < 31);
    Q_ASSERT(level0_blocks() <= pyramid_end() || tail);
}

// The blocks that make up the level-0 blocks [first, last), from the
// coarsest levels that fit: visit(level, index) for each. Like a segment
// tree: take the blocks at the odd ends of the range, then continue one
// level up with the pairs that are fully inside it; on the top level, the
// blocks one by one. Level 0 of a chunk being written has no levels above
// it: its blocks one by one too (the end of the complete chunks is a
// multiple of 2^ChunkLevel, so no pair crosses it).
template <typename Visit>
static void visit_range(const TPeakLevels& levels, qint64 first, qint64 last, Visit&& visit)
{
    Q_ASSERT(levels.level_count() > 0);
    Q_ASSERT(first >= 0 && first < last && last <= levels.block_count(0));

    const qint64 end = std::min(last, levels.pyramid_end());
    for (qint64 index = std::max(first, end); index < last; ++index) {
        visit(0, index);
    }
    last = end;

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
        // The block and its RMS from one record; an RMS code is >= 0
        const qint16* codes = record(level, index);
        combine(result, {codes[2 * channel], codes[2 * channel + 1]});
        const float value = magnitude_value(std::max(int(codes[2 * m_channelCount + channel]), 0));
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


