// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of the curve polyline: built from pixel columns and reduced,
// it must stay within the tolerance of the full polyline and keep spikes.

#include "TCurvePolyline.h"
#include "TCurveSnapshot.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <random>
#include <vector>

static int failures = 0;

static void check(bool ok, const char* what)
{
    std::printf("%s: %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) {
        ++failures;
    }
}

static qreal distance_to_segment(QPointF p, QPointF a, QPointF b)
{
    const QPointF ab = b - a;
    const qreal lengthSquared = QPointF::dotProduct(ab, ab);
    qreal t = lengthSquared > 0.0 ? std::clamp(QPointF::dotProduct(p - a, ab) / lengthSquared, 0.0, 1.0) : 0.0;
    const QPointF offset = p - (a + t * ab);
    return std::sqrt(QPointF::dotProduct(offset, offset));
}

static qreal distance_to_polyline(QPointF p, const QPolygonF& polyline)
{
    qreal nearest = std::numeric_limits<qreal>::max();
    for (qsizetype i = 0; i + 1 < polyline.size(); ++i) {
        nearest = std::min(nearest, distance_to_segment(p, polyline.at(i), polyline.at(i + 1)));
    }
    return nearest;
}

static std::vector<TCurveColumn> columns_of(const std::vector<TCurveSnapshot::Point>& points, double x0, double width, int count)
{
    auto snapshot = TCurveSnapshot::create(points, 1.0);
    std::vector<TCurveColumn> columns(static_cast<size_t>(count));
    TCurveLookupCache cache;
    snapshot->get_columns(x0, width, count, columns.data(), cache);
    return columns;
}

int main()
{
    const qreal height = 100.0;

    // A straight line keeps only its two end points
    {
        auto columns = columns_of({{0.0, 0.0}, {1000.0, 1.0}}, 0.0, 1.0, 1000);
        QPolygonF polyline = curve_polyline(columns.data(), 1000, 0.0, height);
        check(polyline.size() == 2, "a straight line reduces to 2 points");
    }

    // A flat curve is one horizontal line
    {
        auto columns = columns_of({{500.0, 0.25}}, 0.0, 1.0, 1000);
        QPolygonF polyline = curve_polyline(columns.data(), 1000, 0.0, height);
        check(polyline.size() == 2 && polyline.at(0).y() == 75.0 && polyline.at(1).y() == 75.0, "a flat curve is one line");
    }

    // A group of nodes inside one pixel shows its lowest and highest node
    {
        std::vector<TCurveSnapshot::Point> points = {{0.0, 0.5}, {500.10, 0.9}, {500.20, 0.1}, {500.30, 0.6}, {1000.0, 0.5}};
        auto columns = columns_of(points, 0.0, 1.0, 1000);
        QPolygonF polyline = curve_polyline(columns.data(), 1000, 0.0, height);
        bool top = false, bottom = false;
        for (const QPointF& point : polyline) {
            if (std::fabs(point.y() - (height - 0.9 * height)) < 1.0e-9) top = true;
            if (std::fabs(point.y() - (height - 0.1 * height)) < 1.0e-9) bottom = true;
        }
        check(top && bottom, "a group of nodes inside one pixel keeps its spike");
    }

    // Random curves with groups: every point of the full polyline lies within
    // the tolerance of the reduced one
    {
        std::mt19937 rng(3);
        std::uniform_real_distribution<double> unit(0.0, 1.0);
        bool ok = true;
        qsizetype full = 0, reduced = 0;

        for (int round = 0; round < 50; ++round) {
            std::vector<TCurveSnapshot::Point> points;
            double when = 0.0;
            for (int i = 0; i < 60; ++i) {
                when += (round % 2) ? unit(rng) * 3.0 : unit(rng) * 400.0;
                points.push_back({when, unit(rng)});
            }

            auto columns = columns_of(points, -10.0, 1.0, int(when) + 20);
            QPolygonF all = curve_polyline(columns.data(), int(columns.size()), 0.0, height, 0.0);
            QPolygonF less = curve_polyline(columns.data(), int(columns.size()), 0.0, height, 0.5);
            full += all.size();
            reduced += less.size();

            for (const QPointF& point : all) {
                if (distance_to_polyline(point, less) > 0.5 + 1.0e-9) ok = false;
            }
        }
        std::printf("random curves: %lld points without reduction, %lld with\n", qlonglong(full), qlonglong(reduced));
        check(ok, "the reduced polyline stays within 0.5 px");
    }

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all curve polyline tests passed\n");
    return 0;
}
