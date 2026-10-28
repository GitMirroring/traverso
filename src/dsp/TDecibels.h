// SPDX-FileCopyrightText: 2005-2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cmath>

// Decibels and linear gain, for the audio path (curves, automation, sends)
// and the meters alike. Silence is below -120 dB, a gain of 0.000001.

// -120 dB and below: silence (0)
static inline float dB_to_gain(float dB)
{
    return dB > -120.0f ? std::pow(10.0f, dB * 0.05f) : 0.0f;
}

// Silence (below 0.000001) is -120 dB
static inline float gain_to_dB(float gain)
{
    if (gain < 0.000001f) return -120.0f;
    return 20.0f * std::log10(gain);
}
