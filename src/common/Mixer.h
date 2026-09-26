/*
    Copyright (C) 2005-2026 Remon Sijrier
    This file is part of Traverso

    Traverso is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA.

*/

#ifndef TRAVERSO_MIXER_H
#define TRAVERSO_MIXER_H

#include "defines.h"
#include <cmath>

static inline float f_max(float x, float a)
{
    x -= a;
    x += fabsf (x);
    x *= 0.5f;
    x += a;
    return (x);
}

static inline float dB_to_scale_factor (float dB)
{
    return dB > -120.0f ? ::pow(10.0f, dB * 0.05f) : 0.0f;
}

float default_compute_peak             (const audio_sample_t*  buf, nframes_t nsamples, float current);
void  default_apply_gain_to_buffer      (audio_sample_t*  buf, nframes_t nframes, float gain);
void  default_mix_buffers_with_gain     (audio_sample_t*  dst, const audio_sample_t*  src, nframes_t nframes, float gain);
void  default_mix_buffers_no_gain       (audio_sample_t*  dst, const audio_sample_t*  src, nframes_t nframes);

// Use compiler-provided SIMD flags for safe architecture matching
#if defined(__SSE__) && defined(SSE_OPTIMIZATIONS)
extern "C"
{
/* Hardware-accelerated assembly vector functions */
float x86_sse_compute_peak          (const audio_sample_t*  buf, nframes_t nsamples, float current);
void  x86_sse_apply_gain_to_buffer   (audio_sample_t*  buf, nframes_t nframes, float gain);
void  x86_sse_mix_buffers_with_gain  (audio_sample_t*  dst, const audio_sample_t*  src, nframes_t nframes, float gain);
void  x86_sse_mix_buffers_no_gain    (audio_sample_t*  dst, const audio_sample_t*  src, nframes_t nframes);
}
#endif

#if defined (__APPLE__)
float accel_compute_peak              (const audio_sample_t* buf, nframes_t nsamples, float current);
void  accel_apply_gain_to_buffer      (audio_sample_t* buf, nframes_t nframes, float gain);
void  accel_mix_buffers_with_gain     (audio_sample_t* dst, const audio_sample_t* src, nframes_t nframes, float gain);
void  accel_mix_buffers_no_gain       (audio_sample_t* dst, const audio_sample_t* src, nframes_t nframes);
#endif

class Mixer
{
public:
    typedef float (*compute_peak_t)            (const audio_sample_t* , nframes_t, float);
    typedef void  (*apply_gain_to_buffer_t)    (audio_sample_t* , nframes_t, float);
    typedef void  (*mix_buffers_with_gain_t)   (audio_sample_t* , const audio_sample_t* , nframes_t, float);
    typedef void  (*mix_buffers_no_gain_t)     (audio_sample_t* , const audio_sample_t* , nframes_t);

    static compute_peak_t         compute_peak;
    static apply_gain_to_buffer_t    apply_gain_to_buffer;
    static mix_buffers_with_gain_t   mix_buffers_with_gain;
    static mix_buffers_no_gain_t     mix_buffers_no_gain;

    static inline float coefficient_to_dB (float coeff)
    {
        if (coeff < 0.000001f)
            return (-120.0f);
        return 20.0f * log10 (coeff);
    }
};

#endif
