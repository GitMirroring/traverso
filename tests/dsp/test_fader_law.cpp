// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of the fader law in TFaderLaw: dB_to_position() and
// position_to_gain() are each other's inverse, 0 dB sits at
// ZeroDBPosition and the ends of the travel are silence and +12 dB.

#include "TFaderLaw.h"
#include "TGain.h"

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
    return TFaderLaw::dB_to_position(TGain::to_dB(TFaderLaw::position_to_gain(position)));
}

int main()
{
    // The anchor points of the travel
    check(TFaderLaw::dB_to_position(0.0f) == TFaderLaw::ZeroDBPosition, "0 dB sits at ZeroDBPosition");
    check(TFaderLaw::position_to_gain(TFaderLaw::ZeroDBPosition) == 1.0f, "ZeroDBPosition is unity gain");
    check(TFaderLaw::dB_to_position(TGain::MaxDB) == 1.0f, "+12 dB is the top of the travel");
    check(std::fabs(TFaderLaw::position_to_gain(1.0f) - TGain::MaxGain) < 1.0e-6f, "the top of the travel is +12 dB");
    check(TFaderLaw::dB_to_position(TGain::MinDB) == 0.0f, "-120 dB is the bottom of the travel");
    check(TFaderLaw::position_to_gain(0.0f) == 0.0f, "the bottom of the travel is silence");

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
    float previous = TFaderLaw::position_to_gain(0.0f);
    for (int i = 1; i <= 1000; ++i) {
        const float gain = TFaderLaw::position_to_gain(float(i) / 1000.0f);
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
    std::printf("all fader law tests passed\n");
    return 0;
}
