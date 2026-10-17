// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TWaveformColumns.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

static constexpr TWaveformColumn EmptyColumn{std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest()};

TWaveformColumns::Source TWaveformColumns::source_for(const TPeakLevels& levels, const TTimeRef& columnWidth)
{
    if (levels.frame_count() <= 0 || levels.rate() == 0) {
        return Source::None;
    }
    if (TTimeRef::to_frames(columnWidth, levels.rate()) < SourceThreshold) {
        return Source::Samples;
    }
    return levels.level_count() > 0 ? Source::Pyramid : Source::None;
}

float TWaveformColumns::max_amplitude(const TPeakLevels& levels, TWaveformSampleSource* samples, qint64 first, qint64 last)
{
    const int channels = levels.channel_count();
    first = std::max<qint64>(first, 0);
    last = std::min(last, levels.frame_count());
    if (first >= last || channels <= 0) {
        return 0.0f;
    }

    std::vector<float> buffer;
    std::vector<float*> pointers(static_cast<size_t>(channels));
    auto max_of_samples = [&](qint64 from, qint64 to) {
        float amplitude = 0.0f;
        if (from >= to) {
            return amplitude;
        }
        buffer.resize(size_t(to - from) * size_t(channels));
        for (int c = 0; c < channels; ++c) {
            pointers[size_t(c)] = buffer.data() + qsizetype(c) * (to - from);
        }
        const qint64 read = samples->read(from, to - from, pointers.data());
        for (int c = 0; c < channels; ++c) {
            for (qint64 i = 0; i < read; ++i) {
                amplitude = std::max(amplitude, std::fabs(pointers[size_t(c)][i]));
            }
        }
        return amplitude;
    };

    // The level-0 blocks fully inside the range; without them (short range
    // or no pyramid) the samples decide
    const qint64 firstBlock = (first + TPeakLevels::BlockFrames - 1) >> TPeakLevels::BlockFramesShift;
    const qint64 lastBlock = std::min(last >> TPeakLevels::BlockFramesShift, levels.level_count() > 0 ? levels.block_count(0) : 0);
    if (firstBlock >= lastBlock) {
        return max_of_samples(first, last);
    }

    float amplitude = std::max(max_of_samples(first, firstBlock << TPeakLevels::BlockFramesShift),
                               max_of_samples(lastBlock << TPeakLevels::BlockFramesShift, last));

    // A stored block span contains the real one and is at most one
    // quantization step wider, so the real maximum of the blocks is within
    // one step below the bound, and only blocks whose stored amplitude
    // reaches that can hold it
    auto stored_amplitude = [](const TPeakBlock& block) {
        return std::max(std::fabs(TPeakLevels::to_value(block.min)), std::fabs(TPeakLevels::to_value(block.max)));
    };
    float bound = 0.0f;
    for (int c = 0; c < channels; ++c) {
        bound = std::max(bound, stored_amplitude(levels.range(c, firstBlock, lastBlock)));
    }
    // A saturated block (|sample| beyond the stored range) says nothing
    // about how far beyond, so every saturated block is read
    const bool saturated = bound >= float(TPeakLevels::MaxValue) / TPeakLevels::ValueScale;
    if (!saturated && bound <= amplitude) {
        return amplitude;
    }
    const float threshold = saturated ? bound : std::max(amplitude, bound - 1.0f / TPeakLevels::ValueScale);
    for (qint64 block = firstBlock; block < lastBlock; ++block) {
        bool candidate = false;
        for (int c = 0; c < channels && !candidate; ++c) {
            candidate = stored_amplitude(levels.block(0, c, block)) >= threshold;
        }
        if (candidate) {
            const qint64 from = block << TPeakLevels::BlockFramesShift;
            amplitude = std::max(amplitude, max_of_samples(from, std::min(from + TPeakLevels::BlockFrames, last)));
        }
    }
    return amplitude;
}

TWaveformColumns::Source TWaveformColumns::query(const TPeakLevels& levels,
                                                 TWaveformSampleSource* samples,
                                                 const TTimeRef& start,
                                                 const TTimeRef& columnWidth,
                                                 int count,
                                                 TWaveformColumn* out)
{
    Q_ASSERT(columnWidth > TTimeRef());
    Q_ASSERT(count >= 0);

    std::fill(out, out + qsizetype(count) * levels.channel_count(), EmptyColumn);

    switch (source_for(levels, columnWidth)) {
    case Source::Samples:
        return samples ? from_samples(levels, samples, start, columnWidth, count, out) : Source::None;
    case Source::Pyramid:
        return from_pyramid(levels, start, columnWidth, count, out);
    case Source::None:
        break;
    }
    return Source::None;
}

TWaveformColumns::Source TWaveformColumns::from_pyramid(const TPeakLevels& levels,
                                                        const TTimeRef& start, const TTimeRef& columnWidth, int count,
                                                        TWaveformColumn* out)
{
    const uint rate = levels.rate();
    const qint64 frames = levels.frame_count();
    const qint64 blocks = levels.block_count(0);
    const int channels = levels.channel_count();

    // The first level-0 block that starts at or after edge
    auto first_block_at = [&](const TTimeRef& edge) {
        const qint64 frame = std::clamp<qint64>(TTimeRef::frame_at_or_after(edge, rate), 0, frames);
        const qint64 block = (frame + TPeakLevels::BlockFrames - 1) >> TPeakLevels::BlockFramesShift;
        return std::min(block, blocks);
    };

    qint64 first = first_block_at(start);
    for (int i = 0; i < count; ++i) {
        const qint64 last = first_block_at(start + columnWidth * double(i + 1));
        if (last > first) {
            for (int channel = 0; channel < channels; ++channel) {
                const TPeakBlock block = levels.range(channel, first, last);
                out[qsizetype(channel) * count + i] = {TPeakLevels::to_value(block.min), TPeakLevels::to_value(block.max)};
            }
        }
        first = last;
    }

    return Source::Pyramid;
}

TWaveformColumns::Source TWaveformColumns::from_samples(const TPeakLevels& levels, TWaveformSampleSource* samples,
                                                        const TTimeRef& start, const TTimeRef& columnWidth, int count,
                                                        TWaveformColumn* out)
{
    const uint rate = levels.rate();
    const qint64 lastFrame = levels.frame_count() - 1;
    const int channels = levels.channel_count();

    // Every column edge: the frame at or before it and how far past it the edge is
    std::vector<qint64> edgeFrame(size_t(count) + 1);
    std::vector<double> edgeFraction(size_t(count) + 1);
    for (int i = 0; i <= count; ++i) {
        edgeFrame[size_t(i)] = TTimeRef::frame_at_or_before(start + columnWidth * double(i), rate, &edgeFraction[size_t(i)]);
    }

    // The frames the columns need, one neighbour outside included
    const qint64 first = std::max<qint64>(edgeFrame.front(), 0);
    const qint64 last = std::min(edgeFrame.back() + 1, lastFrame);
    if (first > last) {
        return Source::Samples;
    }

    const qint64 wanted = last - first + 1;
    std::vector<float> buffer(size_t(wanted) * size_t(channels));
    std::vector<float*> channelBuffers(static_cast<size_t>(channels));
    for (int channel = 0; channel < channels; ++channel) {
        channelBuffers[size_t(channel)] = buffer.data() + qsizetype(channel) * wanted;
    }

    const qint64 read = samples->read(first, wanted, channelBuffers.data());
    if (read <= 0) {
        return Source::None;
    }
    const qint64 available = first + std::min(read, wanted) - 1;   // last frame with a sample

    for (int channel = 0; channel < channels; ++channel) {
        const float* samplesOfChannel = channelBuffers[size_t(channel)];
        auto sample = [&](qint64 frame) { return samplesOfChannel[frame - first]; };
        TWaveformColumn* columns = out + qsizetype(channel) * count;

        // The value of the line through the samples at edge i, if the line is there
        auto edge_value = [&](int i, float& value) {
            const qint64 frame = edgeFrame[size_t(i)];
            const double fraction = edgeFraction[size_t(i)];
            if (frame < first || frame > available || (fraction > 0.0 && frame + 1 > available)) {
                return false;
            }
            value = fraction > 0.0
                ? float(sample(frame) + fraction * (sample(frame + 1) - sample(frame)))
                : sample(frame);
            return true;
        };

        float entry = 0.0f;
        bool hasEntry = edge_value(0, entry);

        for (int i = 0; i < count; ++i) {
            TWaveformColumn column = EmptyColumn;
            auto add = [&column](float value) {
                column.min = std::min(column.min, value);
                column.max = std::max(column.max, value);
            };

            float exit = 0.0f;
            const bool hasExit = edge_value(i + 1, exit);
            if (hasEntry) {
                add(entry);
            }
            if (hasExit) {
                add(exit);
            }

            // The samples inside the column: the first frame at or after its
            // left edge up to the first frame at or after its right edge
            const qint64 from = std::max(edgeFrame[size_t(i)] + (edgeFraction[size_t(i)] > 0.0 ? 1 : 0), first);
            const qint64 to = std::min(edgeFrame[size_t(i) + 1] + (edgeFraction[size_t(i) + 1] > 0.0 ? 1 : 0) - 1, available);
            for (qint64 frame = from; frame <= to; ++frame) {
                add(sample(frame));
            }

            columns[i] = column;
            entry = exit;
            hasEntry = hasExit;
        }
    }

    return Source::Samples;
}
