// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "TCurveSnapshot.h"

#include <QPolygonF>
#include <algorithm>
#include <utility>
#include <vector>

/**
 * Drops the points of polyline that lie within tolerance of the line through
 * the points that are kept (Ramer-Douglas-Peucker), so no point of the original
 * polyline is further than tolerance from the result. Straight stretches keep
 * only their end points, spikes and bends keep the points they need.
 */
inline QPolygonF reduce_polyline(const QPolygonF& polyline, qreal tolerance)
{
    const qsizetype count = polyline.size();
    if (count < 3) {
        return polyline;
    }

    std::vector<bool> keep(size_t(count), false);
    keep.front() = true;
    keep.back() = true;

    const qreal toleranceSquared = tolerance * tolerance;

    // Ranges still to check, first and last point index
    std::vector<std::pair<qsizetype, qsizetype>> ranges;
    ranges.reserve(64);
    ranges.emplace_back(0, count - 1);

    while (!ranges.empty()) {
        const auto [first, last] = ranges.back();
        ranges.pop_back();

        const QPointF a = polyline.at(first);
        const QPointF b = polyline.at(last);
        const QPointF ab = b - a;
        const qreal lengthSquared = QPointF::dotProduct(ab, ab);

        qreal furthestSquared = 0.0;
        qsizetype furthest = -1;

        for (qsizetype i = first + 1; i < last; ++i) {
            const QPointF ap = polyline.at(i) - a;
            qreal distanceSquared;
            if (lengthSquared > 0.0) {
                // Distance to the line segment a-b
                const qreal t = std::clamp(QPointF::dotProduct(ap, ab) / lengthSquared, 0.0, 1.0);
                const QPointF offset = ap - t * ab;
                distanceSquared = QPointF::dotProduct(offset, offset);
            } else {
                distanceSquared = QPointF::dotProduct(ap, ap);
            }
            if (distanceSquared > furthestSquared) {
                furthestSquared = distanceSquared;
                furthest = i;
            }
        }

        if (furthest >= 0 && furthestSquared > toleranceSquared) {
            keep[size_t(furthest)] = true;
            ranges.emplace_back(first, furthest);
            ranges.emplace_back(furthest, last);
        }
    }

    QPolygonF reduced;
    for (qsizetype i = 0; i < count; ++i) {
        if (keep[size_t(i)]) {
            reduced.append(polyline.at(i));
        }
    }
    return reduced;
}

/**
 * The polyline of a curve from its pixel columns, see TCurveSnapshot::get_columns().
 * Column i spans x + i to x + i + 1; a value v is drawn at height - v * height.
 * Each column contributes where the curve leaves it and, if they differ, its
 * lowest and highest value at their position, so spikes and groups of nodes
 * inside one pixel stay visible. The result is reduced with tolerance pixels,
 * a tolerance of 0 keeps all points.
 */
inline QPolygonF curve_polyline(const TCurveColumn* columns, int count, qreal x, qreal height, qreal tolerance = 0.5)
{
    QPolygonF polyline;
    if (count <= 0) {
        return polyline;
    }

    polyline.reserve(count + 1);

    auto y = [height](double value) { return height - value * height; };
    auto add = [&polyline](const QPointF& point) {
        if (polyline.isEmpty() || polyline.constLast() != point) {
            polyline.append(point);
        }
    };

    add(QPointF(x, y(columns[0].entry)));

    for (int i = 0; i < count; ++i) {
        const TCurveColumn& column = columns[i];
        const qreal left = x + i;

        add(QPointF(left + column.firstExtremeAt, y(column.firstExtreme)));
        add(QPointF(left + column.secondExtremeAt, y(column.secondExtreme)));
        add(QPointF(left + 1, y(column.exit)));
    }

    if (tolerance <= 0.0) {
        return polyline;
    }
    return reduce_polyline(polyline, tolerance);
}
