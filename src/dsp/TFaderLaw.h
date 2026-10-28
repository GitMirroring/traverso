// SPDX-FileCopyrightText: 2005-2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>

// The fader law: where a level sits on a fader's travel (0.0 at the bottom,
// 1.0 at the top), and the limits of the faders.
class TFaderLaw
{
public:
    static constexpr float MinDB   = -120.0f;    // the bottom of the travel: silence
    static constexpr float MaxDB   = 12.0f;      // the top of the travel
    static constexpr float MinGain = 0.000001f;  // below it: silence (-120 dB)
    static constexpr float MaxGain = 3.981072f;  // +12 dB

    // The fader puts 0 dB at ZeroDBPosition of its travel. Below it the gain
    // is warped with LowerExponent, which gives the 0 to -10 dB zone more
    // travel and compresses -30 to -60 dB into a small area; above it
    // UpperExponent spreads 0 to +12 dB over the rest of the travel.
    static constexpr float ZeroDBPosition = 0.85f;
    static constexpr float LowerExponent  = 0.47f;
    static constexpr float UpperExponent  = 0.85f;
    static_assert(ZeroDBPosition > 0.0f && ZeroDBPosition < 1.0f);
    static_assert(LowerExponent > 0.0f && UpperExponent > 0.0f);

    static inline float clamp_dB(float dB)
    {
        return std::clamp(dB, MinDB, MaxDB);
    }

    // Below MinGain silence (0), above MaxGain MaxGain
    static inline float clamp_gain(float gain)
    {
        if (gain < MinGain) return 0.0f;
        if (gain > MaxGain) return MaxGain;
        return gain;
    }

    // A level in dB to its position on the travel
    static float dB_to_position(float dB);

    // A position on the travel to its linear gain
    static float position_to_gain(float position);
};
