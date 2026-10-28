// SPDX-FileCopyrightText: 2005-2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "TTypes.h"
#include "TRealTime.h"

#include <vector>

// The operations on a buffer's samples in the audio path, each the best
// implementation for this machine: the portable C++ loops, the same loops
// built for AVX2, or Apple's Accelerate (T20).
class TBufferOperations
{
public:
    // The peak of buf, or current when that is higher
    using UpdatePeak  = float (*)(const audio_sample_t*, nframes_t, float) T_REALTIME_SAFE;
    using ApplyGain   = void  (*)(audio_sample_t*, nframes_t, float) T_REALTIME_SAFE;
    // dst += src * gain
    using MixWithGain = void  (*)(audio_sample_t*, const audio_sample_t*, nframes_t, float) T_REALTIME_SAFE;
    // dst += src
    using Mix         = void  (*)(audio_sample_t*, const audio_sample_t*, nframes_t) T_REALTIME_SAFE;

    static UpdatePeak  update_peak;
    static ApplyGain   apply_gain;
    static MixWithGain mix_with_gain;
    static Mix         mix;

    // The implementation in use: "portable", "AVX2" or "Apple Accelerate"
    static const char* implementation_name() { return s_implementation; }

    // One implementation of the four operations
    struct Implementation {
        const char*  name;
        UpdatePeak   updatePeak;
        ApplyGain    applyGain;
        MixWithGain  mixWithGain;
        Mix          mix;
    };

    // The implementations this machine can run, the portable one first and
    // the one in use last (for the tests; not real-time safe)
    static std::vector<Implementation> available_implementations();

private:
    // Picks the best implementation, before main() (TBufferOperations.cpp)
    static bool choose_best_implementation();
    static const char* s_implementation;
    static const bool s_bestImplementationChosen;
};
