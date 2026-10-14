// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "defines.h"

#include <memory>
#include <vector>

// Where the last evaluation of a curve was, to find the next one quickly.
// Every reader (the audio thread, the GUI thread) keeps its own.
struct TCurveLookupCache
{
    int segment{-1};
};

/**
 * @brief The solved state of a curve: its points and spline segments.
 *
 * Built once in the GUI thread for each change of the curve and never changed
 * afterwards, so the GUI thread and the audio thread can evaluate the same
 * snapshot at the same time, each with its own TCurveLookupCache.
 *
 * Segments use the constrained cubic spline of CJC Kruger ("Constrained Cubic
 * Spline Interpolation"), with coefficients relative to the start of the
 * segment, so the precision does not depend on how far the curve is on the
 * timeline.
 */
class TCurveSnapshot
{
public:
    struct Point
    {
        double when;
        double value;
    };

    // Points sorted by when; defaultValue is used when there are none
    static std::shared_ptr<const TCurveSnapshot> create(std::vector<Point> points, double defaultValue);

    bool is_empty() const { return m_points.empty(); }
    int point_count() const { return int(m_points.size()); }
    const std::vector<Point>& points() const { return m_points; }

    // The when of the last point, 0 without points
    double get_range() const { return m_points.empty() ? 0.0 : m_points.back().when; }

    // Fills vec with veclen values of the curve between x0 and x1. Real-time safe.
    void get_vector(double x0, double x1, float* vec, nframes_t veclen, TCurveLookupCache& cache) const;

    // The value of the curve at x, for x between the first and the last point
    double evaluate(double x, TCurveLookupCache& cache) const;

private:
    // y = a + b*u + c*u^2 + d*u^3, with u = x - when of the segment's first point
    struct Segment
    {
        double a, b, c, d;
    };

    TCurveSnapshot(std::vector<Point> points, double defaultValue);
    void solve();

    std::vector<Point>   m_points;
    std::vector<Segment> m_segments;   // segment i runs from point i to point i + 1
    double               m_defaultValue;
};
