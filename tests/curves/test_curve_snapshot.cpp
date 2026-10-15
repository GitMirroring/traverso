// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of TCurveSnapshot: compares it with the curve evaluation that
// TCurve used before (absolute polynomial coefficients), and checks that its
// precision does not depend on where the curve is on the timeline.

#include "TCurveSnapshot.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <random>
#include <vector>

using Point = TCurveSnapshot::Point;

// The evaluation TCurve used before TCurveSnapshot: one absolute polynomial per
// segment, stored with the segment's right point. Copied from TCurve.cpp,
// without the lookup cache (which only affects speed).
struct LegacyCurve
{
    struct Node { double when; double value; double coeff[4]; };
    std::vector<Node> nodes;
    double defaultValue{1.0};

    explicit LegacyCurve(const std::vector<Point>& points)
    {
        for (const Point& p : points) {
            nodes.push_back({p.when, p.value, {0, 0, 0, 0}});
        }
        solve();
    }

    void solve()
    {
        int npoints = int(nodes.size());
        if (npoints <= 2) {
            return;
        }
        std::vector<double> x(npoints), y(npoints);
        for (int i = 0; i < npoints; ++i) {
            x[i] = nodes[i].when;
            y[i] = nodes[i].value;
        }
        double lp0 = (x[1] - x[0]) / (y[1] - y[0]);
        double lp1 = (x[2] - x[1]) / (y[2] - y[1]);
        double fpone = ((lp0 * lp1) < 0) ? 0 : 2 / (lp1 + lp0);
        double fplast = 0;
        for (int i = 0; i < npoints; ++i) {
            double xdelta = 0, xdelta2 = 0, ydelta = 0, fpi;
            if (i > 0) {
                xdelta = x[i] - x[i - 1];
                xdelta2 = xdelta * xdelta;
                ydelta = y[i] - y[i - 1];
            }
            if (i == 0) {
                fplast = ((3 * (y[1] - y[0]) / (2 * (x[1] - x[0]))) - (fpone * 0.5));
                continue;
            } else if (i == npoints - 1) {
                fpi = ((3 * ydelta) / (2 * xdelta)) - (fplast * 0.5);
            } else {
                double slope_before = ((x[i + 1] - x[i]) / (y[i + 1] - y[i]));
                double slope_after = (xdelta / ydelta);
                fpi = ((slope_after * slope_before) < 0.0) ? 0.0 : 2 / (slope_before + slope_after);
            }
            double fractal = ((6 * ydelta) / xdelta2);
            double fppL = (((-2 * (fpi + (2 * fplast))) / (xdelta))) + fractal;
            double fppR = (2 * ((2 * fpi) + fplast) / xdelta) - fractal;
            double d = (fppR - fppL) / (6 * xdelta);
            double c = ((x[i] * fppL) - (x[i - 1] * fppR)) / (2 * xdelta);
            double xim12 = x[i - 1] * x[i - 1];
            double xim13 = xim12 * x[i - 1];
            double xi2 = x[i] * x[i];
            double xi3 = xi2 * x[i];
            double b = (ydelta - (c * (xi2 - xim12)) - (d * (xi3 - xim13))) / xdelta;
            nodes[i].coeff[0] = y[i - 1] - (b * x[i - 1]) - (c * xim12) - (d * xim13);
            nodes[i].coeff[1] = b;
            nodes[i].coeff[2] = c;
            nodes[i].coeff[3] = d;
            fplast = fpi;
        }
    }

    double multipoint_eval(double x) const
    {
        auto lower = std::lower_bound(nodes.begin(), nodes.end(), x,
                                      [](const Node& n, double w) { return n.when < w; });
        if (lower != nodes.end() && lower->when == x) {
            return lower->value;
        }
        const Node& cn = *lower;   // first node after x: holds the segment's coefficients
        double x2 = x * x;
        return cn.coeff[0] + (cn.coeff[1] * x) + (cn.coeff[2] * x2) + (cn.coeff[3] * x2 * x);
    }

    void get_vector(double x0, double x1, float* vec, nframes_t veclen) const
    {
        int npoints = int(nodes.size());
        if (npoints == 0) {
            for (nframes_t i = 0; i < veclen; ++i) vec[i] = float(defaultValue);
            return;
        }
        double max_x = nodes.back().when, min_x = nodes.front().when;
        double lx = std::max(min_x, x0);
        if (x1 < 0) x1 = nodes.back().when;
        double hx = std::min(max_x, x1);
        nframes_t original_veclen = veclen;
        if (x0 < min_x) {
            double frac = (min_x - x0) / (x1 - x0);
            nframes_t subveclen = std::min(nframes_t(std::floor(veclen * frac)), veclen);
            for (nframes_t i = 0; i < subveclen; ++i) vec[i] = float(nodes.front().value);
            veclen -= subveclen;
            vec += subveclen;
        }
        if (veclen && x1 > max_x) {
            double frac = (x1 - max_x) / (x1 - x0);
            nframes_t subveclen = std::min(nframes_t(std::floor(original_veclen * frac)), veclen);
            for (nframes_t i = veclen - subveclen; i < veclen; ++i) vec[i] = float(nodes.back().value);
            veclen -= subveclen;
        }
        if (veclen == 0) return;
        if (npoints == 1) {
            for (nframes_t i = 0; i < veclen; ++i) vec[i] = float(nodes.front().value);
            return;
        }
        if (npoints == 2) {
            double dx = (veclen > 1) ? (hx - lx) / (veclen - 1) : 0;
            double slope = (nodes.back().value - nodes.front().value) / (nodes.back().when - nodes.front().when);
            double yfrac = dx * slope;
            vec[0] = float(nodes.front().value + slope * (lx - nodes.front().when));
            for (nframes_t i = 1; i < veclen; ++i) vec[i] = float(vec[i - 1] + float(yfrac));
            return;
        }
        double rx = lx;
        if (veclen > 1) {
            double dx = (hx - lx) / veclen;
            for (nframes_t i = 0; i < veclen; ++i, rx += dx) vec[i] = float(multipoint_eval(rx));
        }
    }
};

static std::vector<Point> random_curve(std::mt19937& rng, int npoints, double start, double span)
{
    std::uniform_real_distribution<double> whenDist(start, start + span);
    std::uniform_real_distribution<double> valueDist(0.0, 2.0);

    std::vector<double> whens;
    while (int(whens.size()) < npoints) {
        double when = std::floor(whenDist(rng));
        if (std::find(whens.begin(), whens.end(), when) == whens.end()) {
            whens.push_back(when);
        }
    }
    std::sort(whens.begin(), whens.end());

    std::vector<Point> points;
    for (double when : whens) {
        points.push_back({when, valueDist(rng)});
    }
    return points;
}

static int failures = 0;

static void check(bool ok, const char* what)
{
    if (!ok) {
        ++failures;
        std::printf("FAILED: %s\n", what);
    }
}

// The new evaluation gives the same results as the old one
static void test_same_as_legacy()
{
    std::mt19937 rng(20261015);
    double worst = 0.0;
    int unset = 0;

    for (int npoints : {0, 1, 2, 3, 4, 5, 8, 12, 20}) {
        for (int round = 0; round < 200; ++round) {
            const double span = 1.0e7;
            std::vector<Point> points = random_curve(rng, npoints, 0.0, span);

            LegacyCurve legacy(points);
            auto snapshot = TCurveSnapshot::create(points, 1.0);

            const double first = points.empty() ? 0.0 : points.front().when;
            const double last = points.empty() ? span : points.back().when;

            struct Window { double x0, x1; };
            const Window windows[] = {
                {first, last},                                  // the whole curve
                {first + (last - first) * 0.3, first + (last - first) * 0.6},
                {first - span * 0.2, first + (last - first) * 0.5},   // starts before the first point
                {first + (last - first) * 0.5, last + span * 0.2},    // ends after the last point
                {first - span * 0.3, last + span * 0.3},        // both
            };

            for (const Window& window : windows) {
                for (nframes_t veclen : {nframes_t(2), nframes_t(7), nframes_t(64), nframes_t(1024)}) {
                    // The legacy evaluation leaves the value unset when one value
                    // remains between the points: NaN marks those
                    std::vector<float> oldValues(veclen, std::numeric_limits<float>::quiet_NaN());
                    std::vector<float> newValues(veclen);
                    TCurveLookupCache cache;

                    legacy.get_vector(window.x0, window.x1, oldValues.data(), veclen);
                    snapshot->get_vector(window.x0, window.x1, newValues.data(), veclen, cache);

                    for (nframes_t i = 0; i < veclen; ++i) {
                        if (std::isnan(oldValues[i])) {
                            ++unset;
                            continue;
                        }
                        worst = std::max(worst, double(std::fabs(oldValues[i] - newValues[i])));
                    }
                }
            }
        }
    }

    // The legacy evaluation itself loses some precision with 1e7 frames curves
    std::printf("same as legacy: largest difference %.3g, %d values the legacy evaluation left unset\n", worst, unset);
    check(worst < 1.0e-3, "the new evaluation differs from the legacy evaluation");
}

// Moving a curve along the timeline must not change its values. The old
// evaluation loses precision far on the timeline, the new one must not.
static void test_precision_far_on_timeline()
{
    std::mt19937 rng(42);
    const double tenMs = 282240.0;               // 10 ms in universal frames
    const double oneHour = 101606400000.0;       // 1 hour in universal frames

    std::vector<Point> nearPoints = random_curve(rng, 6, 0.0, 6 * tenMs);
    std::vector<Point> farPoints = nearPoints;
    for (Point& point : farPoints) {
        point.when += oneHour;
    }

    auto nearSnapshot = TCurveSnapshot::create(nearPoints, 1.0);
    auto farSnapshot = TCurveSnapshot::create(farPoints, 1.0);
    LegacyCurve nearLegacy(nearPoints);
    LegacyCurve farLegacy(farPoints);

    const nframes_t veclen = 4096;
    std::vector<float> nearValues(veclen), farValues(veclen), nearOld(veclen), farOld(veclen);
    TCurveLookupCache nearCache, farCache;

    const double x0 = nearPoints.front().when, x1 = nearPoints.back().when;
    nearSnapshot->get_vector(x0, x1, nearValues.data(), veclen, nearCache);
    farSnapshot->get_vector(x0 + oneHour, x1 + oneHour, farValues.data(), veclen, farCache);
    nearLegacy.get_vector(x0, x1, nearOld.data(), veclen);
    farLegacy.get_vector(x0 + oneHour, x1 + oneHour, farOld.data(), veclen);

    double newError = 0.0, oldError = 0.0;
    for (nframes_t i = 0; i < veclen; ++i) {
        newError = std::max(newError, double(std::fabs(nearValues[i] - farValues[i])));
        oldError = std::max(oldError, double(std::fabs(nearOld[i] - farOld[i])));
    }

    std::printf("one hour on the timeline, 10 ms segments: new error %.3g, legacy error %.3g\n", newError, oldError);
    check(newError < 1.0e-5, "the new evaluation loses precision far on the timeline");
}

// Points at the same position must not produce NaN or infinity
static void test_points_at_the_same_position()
{
    std::vector<Point> points = {{0, 0.0}, {100, 1.0}, {100, 0.5}, {200, 1.0}, {300, 0.2}};
    auto snapshot = TCurveSnapshot::create(points, 1.0);

    std::vector<float> values(512);
    TCurveLookupCache cache;
    snapshot->get_vector(-50, 350, values.data(), nframes_t(values.size()), cache);

    bool finite = std::all_of(values.begin(), values.end(), [](float v) { return std::isfinite(v); });
    std::printf("points at the same position: %s\n", finite ? "finite" : "NOT finite");
    check(finite, "points at the same position give NaN or infinity");
}

// The constrained spline never turns between two points: every segment stays
// between the values of its two points. get_columns() relies on this.
static void test_segments_do_not_turn()
{
    std::mt19937 rng(11);
    bool ok = true;

    for (int round = 0; round < 300; ++round) {
        std::vector<Point> points = random_curve(rng, 3 + round % 40, 0.0, 1.0e7);
        auto snapshot = TCurveSnapshot::create(points, 1.0);
        TCurveLookupCache cache;

        for (size_t i = 0; i + 1 < points.size(); ++i) {
            const double low = std::min(points[i].value, points[i + 1].value);
            const double high = std::max(points[i].value, points[i + 1].value);
            for (int sample = 1; sample < 1000; ++sample) {
                double x = points[i].when + (points[i + 1].when - points[i].when) * sample / 1000.0;
                double value = snapshot->value_at(x, cache);
                if (value < low - 1.0e-9 || value > high + 1.0e-9) ok = false;
            }
        }
    }

    std::printf("segments do not turn: %s\n", ok ? "ok" : "FAILED");
    check(ok, "a segment turns between its points");
}

// Columns: every value of the curve inside a column lies between its lowest and
// highest value, the reported extremes are values of the curve, and each column
// starts where the previous one ended
static void test_columns()
{
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    bool ok = true;

    for (int npoints : {0, 1, 2, 3, 5, 12, 40}) {
        for (int round = 0; round < 50; ++round) {
            const double span = 1.0e7;
            std::vector<Point> points = random_curve(rng, npoints, 0.0, span);
            auto snapshot = TCurveSnapshot::create(points, 1.0);

            const int count = 64;
            const double width = span * (0.2 + unit(rng)) / count;
            const double x0 = -span * 0.1 + unit(rng) * span * 0.2;
            std::vector<TCurveColumn> columns(count);
            TCurveLookupCache cache;
            snapshot->get_columns(x0, width, count, columns.data(), cache);

            for (int i = 0; i < count; ++i) {
                const TCurveColumn& column = columns[size_t(i)];
                const double start = x0 + i * width;
                const double low = std::min(column.firstExtreme, column.secondExtreme);
                const double high = std::max(column.firstExtreme, column.secondExtreme);
                const double eps = 1.0e-9;

                if (i > 0 && column.entry != columns[size_t(i) - 1].exit) ok = false;
                if (column.firstExtremeAt > column.secondExtremeAt) ok = false;

                for (int sample = 0; sample <= 200; ++sample) {
                    double value = snapshot->value_at(start + width * sample / 200.0, cache);
                    if (value < low - eps || value > high + eps) ok = false;
                }
                for (const Point& point : points) {
                    if (point.when >= start && point.when < start + width && (point.value < low - eps || point.value > high + eps)) ok = false;
                }

                // A reported extreme is a value of the curve where it is reported
                // (a point's own value where two points share a position)
                double firstAt = snapshot->value_at(start + column.firstExtremeAt * width, cache);
                double secondAt = snapshot->value_at(start + column.secondExtremeAt * width, cache);
                bool isPointValue = false;
                for (const Point& point : points) {
                    if (point.value == column.firstExtreme || point.value == column.secondExtreme) isPointValue = true;
                }
                if (!isPointValue && (std::fabs(firstAt - column.firstExtreme) > 1.0e-6 || std::fabs(secondAt - column.secondExtreme) > 1.0e-6)) ok = false;
            }
        }
    }

    // A group of 100 nodes inside one column: its lowest and highest node show
    std::vector<Point> group = {{0.0, 0.5}};
    double groupLow = 0.5, groupHigh = 0.5;
    for (int i = 1; i <= 100; ++i) {
        double value = unit(rng);
        group.push_back({1.0e6 + i, value});
        groupLow = std::min(groupLow, value);
        groupHigh = std::max(groupHigh, value);
    }
    group.push_back({2.0e6, 0.5});
    auto snapshot = TCurveSnapshot::create(group, 1.0);
    std::vector<TCurveColumn> columns(20);
    TCurveLookupCache cache;
    snapshot->get_columns(0.0, 1.0e5, 20, columns.data(), cache);   // the group is in column 10
    const TCurveColumn& column = columns[10];
    const double low = std::min(column.firstExtreme, column.secondExtreme);
    const double high = std::max(column.firstExtreme, column.secondExtreme);
    if (low != groupLow || high != groupHigh) ok = false;

    std::printf("columns: %s\n", ok ? "ok" : "FAILED");
    check(ok, "pixel columns");
}

int main()
{
    test_same_as_legacy();
    test_precision_far_on_timeline();
    test_points_at_the_same_position();
    test_segments_do_not_turn();
    test_columns();

    if (failures) {
        std::printf("%d test(s) failed\n", failures);
        return 1;
    }
    std::printf("all curve tests passed\n");
    return 0;
}
