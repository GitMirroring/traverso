/*
    Copyright (C) 2005-2026 Remon Sijrier
    This file is part of Traverso
*/

#pragma once

#include "defines.h"
#include "TRealTime.h"
#include <cmath>

static inline float f_max(float x, float a)
{
    x -= a;
    x += fabsf (x);
    x *= 0.5f;
    x += a;
    return (x);
}

float portable_update_peak    (const audio_sample_t*  buf, nframes_t nframes, float current) T_REALTIME_SAFE;
void  portable_apply_gain     (audio_sample_t*  buf, nframes_t nframes, float gain) T_REALTIME_SAFE;
void  portable_mix_with_gain  (audio_sample_t*  dst, const audio_sample_t*  src, nframes_t nframes, float gain) T_REALTIME_SAFE;
void  portable_mix            (audio_sample_t*  dst, const audio_sample_t*  src, nframes_t nframes) T_REALTIME_SAFE;

#if defined(__SSE__) && defined(SSE_OPTIMIZATIONS)
extern "C"
{
float x86_sse_compute_peak          (const audio_sample_t*  buf, nframes_t nsamples, float current) T_REALTIME_SAFE;
void  x86_sse_apply_gain_to_buffer   (audio_sample_t*  buf, nframes_t nframes, float gain) T_REALTIME_SAFE;
void  x86_sse_mix_buffers_with_gain  (audio_sample_t*  dst, const audio_sample_t*  src, nframes_t nframes, float gain) T_REALTIME_SAFE;
void  x86_sse_mix_buffers_no_gain    (audio_sample_t*  dst, const audio_sample_t*  src, nframes_t nframes) T_REALTIME_SAFE;
}
#endif

#if defined (Q_OS_MACOS)
float accel_compute_peak              (const audio_sample_t* buf, nframes_t nsamples, float current) T_REALTIME_SAFE;
void  accel_apply_gain_to_buffer      (audio_sample_t* buf, nframes_t nframes, float gain) T_REALTIME_SAFE;
void  accel_mix_buffers_with_gain     (audio_sample_t* dst, const audio_sample_t* src, nframes_t nframes, float gain) T_REALTIME_SAFE;
void  accel_mix_buffers_no_gain       (audio_sample_t* dst, const audio_sample_t* src, nframes_t nframes) T_REALTIME_SAFE;
#endif

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
};
