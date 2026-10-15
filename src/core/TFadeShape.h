// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "TCurveSnapshot.h"

#include <QPointF>

#include <array>
#include <vector>

/**
 * @brief The shape of a fade, independent of its length.
 *
 * The shape is a cubic Bezier curve through four control points, which the
 * mode and the bend and strength factors place. Twelve points on it, with
 * time and value from 0 to 1, are the points of the fade curve; a fade of any
 * length evaluates that one shape at position / length.
 */
class TFadeShape
{
public:
    enum Mode {
        Bended = 0,
        SShape = 1,
        Long = 2
    };

    static const int POINT_COUNT = 12;

    // The four control points for mode, bend and strength (both 0..1)
    static std::array<QPointF, 4> control_points(bool fadeOut, int mode, double bend, double strength);

    // The points of the fade curve, time and value 0..1. A fade in rises from
    // 0 to 1, a fade out falls from 1 to 0.
    static std::vector<TCurveSnapshot::Point> points(bool fadeOut, const std::array<QPointF, 4>& controlPoints);

    // The shape a new fade starts with: the "Fast" preset in the Long mode
    static bool is_default(int mode, double bend, double strength);
};
