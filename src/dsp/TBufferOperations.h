// SPDX-FileCopyrightText: 2005-2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "defines.h"
#include "TRealTime.h"

// The operations on a buffer's samples in the audio path, each the best
// implementation for this machine: the portable C++ loops, SSE assembly or
// Apple's Accelerate.
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

    // The implementation in use: "portable", "SSE" or "Apple Accelerate"
    static const char* implementation_name() { return s_implementation; }

private:
    // Picks the best implementation, before main() (TBufferOperations.cpp)
    static bool choose_best_implementation();
    static const char* s_implementation;
    static const bool s_bestImplementationChosen;
};
