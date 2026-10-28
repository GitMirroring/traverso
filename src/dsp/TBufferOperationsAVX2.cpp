// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// The buffer operations built for AVX2: CMake compiles this file with
// -mavx2 (x86-64 with GCC or Clang); TBufferOperations uses them when
// TCpuFeatures reports AVX2. The loops are those of the portable path.

#include "TBufferOperationsLoops.h"

float avx2_update_peak(const audio_sample_t* buf, nframes_t nframes, float current) T_REALTIME_SAFE
{
    return buffer_loop_update_peak(buf, nframes, current);
}

void avx2_apply_gain(audio_sample_t* buf, nframes_t nframes, float gain) T_REALTIME_SAFE
{
    buffer_loop_apply_gain(buf, nframes, gain);
}

void avx2_mix_with_gain(audio_sample_t* dst, const audio_sample_t* src, nframes_t nframes, float gain) T_REALTIME_SAFE
{
    buffer_loop_mix_with_gain(dst, src, nframes, gain);
}

void avx2_mix(audio_sample_t* dst, const audio_sample_t* src, nframes_t nframes) T_REALTIME_SAFE
{
    buffer_loop_mix(dst, src, nframes);
}
