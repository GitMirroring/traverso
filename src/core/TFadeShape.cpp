// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TFadeShape.h"


std::array<QPointF, 4> TFadeShape::control_points(FadeType type, int mode, double bend, double strength)
{
    std::array<QPointF, 4> points = {QPointF(0.0, 0.0), QPointF(0.25, 0.25), QPointF(0.75, 0.75), QPointF(1.0, 1.0)};

    if (mode == Bended) {
        if (type == FadeIn) {
            points[1] = QPointF(strength * (1.0 - bend), strength * bend);
            points[2] = QPointF(1.0 - (strength * bend), 1.0 - (strength * (1.0 - bend)));
        } else {
            points[1] = QPointF(strength * bend, strength * (1.0 - bend));
            points[2] = QPointF(1.0 - (strength * (1.0 - bend)), 1.0 - (strength * bend));
        }
    } else if (mode == SShape) {
        points[1] = QPointF(strength * (1.0 - bend), strength * bend);
        points[2] = QPointF(1.0 - (strength * (1.0 - bend)), 1.0 - (strength * bend));
    } else if (mode == Long) {
        if (type == FadeIn) {
            points[1] = QPointF(strength * (1.0 - bend), strength * bend);
            points[2] = QPointF(1.0, 1.0);
        } else {
            points[1] = QPointF(0.0, 0.0);
            points[2] = QPointF(1.0 - (strength * (1.0 - bend)), 1.0 - (strength * bend));
        }
    }

    return points;
}

std::vector<TCurveSnapshot::Point> TFadeShape::points(FadeType type, const std::array<QPointF, 4>& controlPoints)
{
    std::vector<TCurveSnapshot::Point> points;
    points.reserve(POINT_COUNT);

    for (int i = 0; i < POINT_COUNT; ++i) {
        const double f = double(i) / (POINT_COUNT - 1);
        const double g = 1.0 - f;

        // The cubic Bezier curve at f
        const double x = controlPoints[0].x() * g * g * g
                       + 3 * controlPoints[1].x() * f * g * g
                       + 3 * controlPoints[2].x() * f * f * g
                       + controlPoints[3].x() * f * f * f;

        double y = controlPoints[0].y() * g * g * g
                 + 3 * controlPoints[1].y() * f * g * g
                 + 3 * controlPoints[2].y() * f * f * g
                 + controlPoints[3].y() * f * f * f;

        if (type == FadeOut) {
            y = 1.0 - y;
        }

        points.push_back({x, y});
    }

    return points;
}

double TFadeShape::effective_length(double length)
{
    return (length <= 1.0) ? 0.0 : length;
}
