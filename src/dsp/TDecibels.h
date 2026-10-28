// SPDX-FileCopyrightText: 2005-2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <cmath>

// Decibels and linear gain, for the audio path (curves, automation, sends,
// faders) and the meters alike.

// The gain range of the audio engine: from silence (-120 dB, a gain of
// 0.000001) to +12 dB
struct TGainRange
{
    static constexpr float MinDB   = -120.0f;
    static constexpr float MaxDB   = 12.0f;
    static constexpr float MinGain = 0.000001f;
    static constexpr float MaxGain = 3.981072f;

    static float clamp_dB(float dB)
    {
        return std::clamp(dB, MinDB, MaxDB);
    }

    // Below MinGain silence (0), above MaxGain MaxGain
    static float clamp_gain(float gain)
    {
        if (gain < MinGain) return 0.0f;
        if (gain > MaxGain) return MaxGain;
        return gain;
    }
};

// MinDB and below: silence (0)
static inline float dB_to_gain(float dB)
{
    return dB > TGainRange::MinDB ? std::pow(10.0f, dB * 0.05f) : 0.0f;
}

// Silence (below MinGain) is MinDB
static inline float gain_to_dB(float gain)
{
    if (gain < TGainRange::MinGain) return TGainRange::MinDB;
    return 20.0f * std::log10(gain);
}
