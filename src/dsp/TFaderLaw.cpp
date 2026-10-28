// SPDX-FileCopyrightText: 2005-2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TFaderLaw.h"
#include "TGain.h"

#include <cmath>

float TFaderLaw::dB_to_position(float dB)
{
    if (dB <= TGain::MinDB) return 0.0f;
    if (dB >= TGain::MaxDB) return 1.0f;

    float gain = TGain::from_dB(dB);

    if (dB < 0.0f) {
        // Normalize the gain between absolute silence and 0 dB (1.0f), then
        // warp it with LowerExponent
        float minGain = TGain::MinGain;
        float normalizedGain = (gain - minGain) / (1.0f - minGain);

        return ZeroDBPosition * std::pow(std::clamp(normalizedGain, 0.0f, 1.0f), LowerExponent);
    } else {
        // 0 dB to +12 dB in the travel above ZeroDBPosition
        float maxGain = TGain::MaxGain;
        float normalizedGain = (gain - 1.0f) / (maxGain - 1.0f);

        return ZeroDBPosition + ((1.0f - ZeroDBPosition) * std::pow(std::clamp(normalizedGain, 0.0f, 1.0f), UpperExponent));
    }
}

float TFaderLaw::position_to_gain(float position)
{
    if (position <= 0.0f) return 0.0f;
    if (position >= 1.0f) return TGain::MaxGain;

    if (position < ZeroDBPosition) {
        // The inverse of the warp below 0 dB in dB_to_position()
        float normalizedGain = std::pow(position / ZeroDBPosition, 1.0f / LowerExponent);
        float minGain = TGain::MinGain;
        return (normalizedGain * (1.0f - minGain)) + minGain;
    } else {
        // The inverse of the warp above 0 dB
        float normalizedGain = std::pow((position - ZeroDBPosition) / (1.0f - ZeroDBPosition), 1.0f / UpperExponent);
        float maxGain = TGain::MaxGain;
        return (normalizedGain * (maxGain - 1.0f)) + 1.0f;
    }
}
