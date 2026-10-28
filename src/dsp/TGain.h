// SPDX-FileCopyrightText: 2005-2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <cmath>

// Gain in the audio engine: linear gain and decibels, and the range from
// silence (-120 dB, a gain of 0.000001) to +12 dB. For the audio path
// (curves, automation, sends, faders) and the meters alike.
struct TGain
{
    static constexpr float MinDB   = -120.0f;
    static constexpr float MaxDB   = 12.0f;
    static constexpr float MinGain = 0.000001f;
    static constexpr float MaxGain = 3.981072f;

    // MinDB and below: silence (0)
    static float from_dB(float dB)
    {
        return dB > MinDB ? std::pow(10.0f, dB * 0.05f) : 0.0f;
    }

    // Silence (below MinGain) is MinDB
    static float to_dB(float gain)
    {
        if (gain < MinGain) return MinDB;
        return 20.0f * std::log10(gain);
    }

    // Below MinGain silence (0), above MaxGain MaxGain
    static float clamp(float gain)
    {
        if (gain < MinGain) return 0.0f;
        if (gain > MaxGain) return MaxGain;
        return gain;
    }

    static float clamp_dB(float dB)
    {
        return std::clamp(dB, MinDB, MaxDB);
    }
};
