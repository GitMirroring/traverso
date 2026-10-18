// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of the fader curve in Mixer: db_to_fader_position() and
// fader_position_to_gain() are each other's inverse, 0 dB sits at
// ZeroDbPosition and the ends of the travel are silence and +12 dB.

#include "Mixer.h"

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

static float position_round_trip(float position)
{
    return Mixer::db_to_fader_position(Mixer::coefficient_to_dB(Mixer::fader_position_to_gain(position)));
}

int main()
{
    // The anchor points of the travel
    check(Mixer::db_to_fader_position(0.0f) == Mixer::ZeroDbPosition, "0 dB sits at ZeroDbPosition");
    check(Mixer::fader_position_to_gain(Mixer::ZeroDbPosition) == 1.0f, "ZeroDbPosition is unity gain");
    check(Mixer::db_to_fader_position(Mixer::max_fader_dB()) == 1.0f, "+12 dB is the top of the travel");
    check(std::fabs(Mixer::fader_position_to_gain(1.0f) - Mixer::max_fader_gain()) < 1.0e-6f, "the top of the travel is +12 dB");
    check(Mixer::db_to_fader_position(Mixer::min_fader_dB()) == 0.0f, "-120 dB is the bottom of the travel");
    check(Mixer::fader_position_to_gain(0.0f) == 0.0f, "the bottom of the travel is silence");

    // Position -> gain -> position, below and above 0 dB
    float worst = 0.0f;
    for (int i = 1; i < 100; ++i) {
        const float position = float(i) / 100.0f;
        worst = std::fmax(worst, std::fabs(position_round_trip(position) - position));
    }
    std::printf("worst round-trip error: %g\n", double(worst));
    check(worst < 1.0e-4f, "position -> gain -> position round trip");

    // Monotonic: more travel is always more gain
    bool monotonic = true;
    float previous = Mixer::fader_position_to_gain(0.0f);
    for (int i = 1; i <= 1000; ++i) {
        const float gain = Mixer::fader_position_to_gain(float(i) / 1000.0f);
        if (gain <= previous) {
            monotonic = false;
        }
        previous = gain;
    }
    check(monotonic, "the gain rises with the travel");

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all Mixer tests passed\n");
    return 0;
}
