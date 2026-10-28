// SPDX-FileCopyrightText: 2005-2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The fader law: where a level sits on a fader's travel (0.0 at the bottom,
// TGainRange::MinDB; 1.0 at the top, TGainRange::MaxDB). How a fader is
// operated, not audio: it goes along to the controllers with the fader port
// of branch 3.
class TFaderLaw
{
public:
    // The fader puts 0 dB at ZeroDBPosition of its travel. Below it the gain
    // is warped with LowerExponent, which gives the 0 to -10 dB zone more
    // travel and compresses -30 to -60 dB into a small area; above it
    // UpperExponent spreads 0 to +12 dB over the rest of the travel.
    static constexpr float ZeroDBPosition = 0.85f;
    static constexpr float LowerExponent  = 0.47f;
    static constexpr float UpperExponent  = 0.85f;
    static_assert(ZeroDBPosition > 0.0f && ZeroDBPosition < 1.0f);
    static_assert(LowerExponent > 0.0f && UpperExponent > 0.0f);

    // A level in dB to its position on the travel
    static float dB_to_position(float dB);

    // A position on the travel to its linear gain
    static float position_to_gain(float position);
};
