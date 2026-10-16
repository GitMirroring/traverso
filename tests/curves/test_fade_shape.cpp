// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of TFadeShape: for every preset, mode and fade type the shape
// starts and ends at the right value, moves one way only, and the fade curve
// through its points stays between 0 and 1.

#include "TFadeShape.h"

#include <cmath>
#include <cstdio>

static int failures = 0;

static void check(bool ok, const char* what)
{
    std::printf("%s: %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) {
        ++failures;
    }
}

int main()
{
    // The presets of resources/fadeshapes.xml: bend, strength
    const double presets[][2] = {{0.0, 0.75}, {0.0, 0.5}, {0.5, 0.5}, {1.0, 0.5}, {1.0, 0.75}};

    bool endsOk = true, monotone = true, inRange = true;

    for (TFadeShape::FadeType type : {TFadeShape::FadeIn, TFadeShape::FadeOut}) {
        for (int mode : {int(TFadeShape::Bended), int(TFadeShape::SShape), int(TFadeShape::Long)}) {
            for (const auto& preset : presets) {
                auto points = TFadeShape::points(type, TFadeShape::control_points(type, mode, preset[0], preset[1]));

                if (int(points.size()) != TFadeShape::POINT_COUNT) endsOk = false;
                const double start = (type == TFadeShape::FadeOut) ? 1.0 : 0.0;
                if (points.front().when != 0.0 || points.front().value != start) endsOk = false;
                if (std::fabs(points.back().when - 1.0) > 1.0e-12 || std::fabs(points.back().value - (1.0 - start)) > 1.0e-12) endsOk = false;

                for (size_t i = 1; i < points.size(); ++i) {
                    if (points[i].when < points[i - 1].when) monotone = false;
                    const double step = points[i].value - points[i - 1].value;
                    if ((type == TFadeShape::FadeOut) ? step > 1.0e-12 : step < -1.0e-12) monotone = false;
                }

                // The fade curve through the points, sampled densely
                auto snapshot = TCurveSnapshot::create(points, 1.0);
                TCurveLookupCache cache;
                for (int sample = 0; sample <= 1000; ++sample) {
                    const double value = snapshot->value_at(sample / 1000.0, cache);
                    if (value < -1.0e-9 || value > 1.0 + 1.0e-9) inRange = false;
                }
            }
        }
    }

    check(endsOk, "every shape starts and ends at the right value");
    check(monotone, "every shape moves one way only");
    check(inRange, "every fade curve stays between 0 and 1");

    // Linear: bend 0.5 and strength 0.5 put the control points on the diagonal
    {
        auto points = TFadeShape::points(TFadeShape::FadeIn, TFadeShape::control_points(TFadeShape::FadeIn, TFadeShape::SShape, 0.5, 0.5));
        bool linear = true;
        for (const auto& point : points) {
            if (std::fabs(point.value - point.when) > 1.0e-12) linear = false;
        }
        check(linear, "the Linear preset is a straight line");
    }

    // One universal frame or less is off
    check(TFadeShape::effective_length(1.0) == 0.0 && TFadeShape::effective_length(0.99) == 0.0
              && TFadeShape::effective_length(-5.0) == 0.0 && TFadeShape::effective_length(1.5) == 1.5,
          "one universal frame or less is off");

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all fade shape tests passed\n");
    return 0;
}
