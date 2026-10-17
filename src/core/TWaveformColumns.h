// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "TPeakPyramid.h"
#include "TTimeRef.h"

// One drawn column of a waveform: the lowest and highest value of the signal
// inside the column, 1.0 = full scale. A column without signal (before the
// start or after the end of the source) has min > max.
struct TWaveformColumn
{
    float min;
    float max;

    bool is_empty() const { return min > max; }
};

// Gives the samples of a source to TWaveformColumns
class TWaveformSampleSource
{
public:
    virtual ~TWaveformSampleSource() = default;

    // Reads count frames from firstFrame (>= 0) into channels[c][0 .. count)
    // for every channel. Returns the number of frames read.
    virtual qint64 read(qint64 firstFrame, qint64 count, float* const* channels) = 0;
};

/**
 * @brief Waveform columns for a view: the model decides where they come from.
 *
 * Column i covers the source time [start + i * width, start + (i + 1) * width).
 * The frames of the source that are inside it belong to it, so the columns
 * partition the frames. The source of the data is chosen for the whole
 * request, so a view shows one kind of data at a time:
 *
 * - Samples, when a column holds fewer than SourceThreshold frames: the signal
 *   is the line through the samples, a column holds the samples inside it and
 *   the line's values at both its edges. The columns of a line touch, and a
 *   column between two samples holds the part of the line that crosses it.
 * - Pyramid otherwise: a level-0 block belongs to the column that holds its
 *   first frame, the column combines its blocks from the coarsest levels that
 *   fit (O(log) per column at any zoom). Values are rounded outwards by at
 *   most 1 / TPeakLevels::ValueScale.
 *
 * No interpolation between pyramid levels: every column shows values that are
 * in the audio, so spikes do not change size while zooming.
 */
class TWaveformColumns
{
public:
    enum class Source
    {
        None,       // nothing to show, or the pyramid is needed but missing
        Samples,
        Pyramid
    };

    // Frames per column below which the samples are read
    static constexpr double SourceThreshold = TPeakLevels::BlockFrames;

    // Which source a request with this column width uses
    static Source source_for(const TPeakLevels& levels, const TTimeRef& columnWidth);

    // Fills out[channel * count + i] for every channel of levels.
    // samples may be nullptr if the request never needs them.
    static Source query(const TPeakLevels& levels,
                        TWaveformSampleSource* samples,
                        const TTimeRef& start,
                        const TTimeRef& columnWidth,
                        int count,
                        TWaveformColumn* out);

private:
    static Source from_samples(const TPeakLevels& levels, TWaveformSampleSource* samples,
                               const TTimeRef& start, const TTimeRef& columnWidth, int count,
                               TWaveformColumn* out);
    static Source from_pyramid(const TPeakLevels& levels,
                               const TTimeRef& start, const TTimeRef& columnWidth, int count,
                               TWaveformColumn* out);
};
