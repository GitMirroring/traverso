/*
    Copyright (C) 2005-2026 Remon Sijrier
    This file is part of Traverso
*/

#ifndef TRAVERSO_MIXER_H
#define TRAVERSO_MIXER_H

#include "defines.h"
#include <cmath>
#include <algorithm>

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

#if defined(__SSE__) && defined(SSE_OPTIMIZATIONS)
extern "C"
{
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

    // --- CENTRALIZED FADER CONSTANTS AND BOUNDARIES ---
    static inline float min_fader_dB()   { return -120.0f; } // Audio engine mathematical floor
    static inline float max_fader_dB()   { return 12.0f; }   // Audio engine mathematical ceiling
    static inline float min_fader_gain() { return 0.000001f; } // Threshold for absolute silence (-120 dB)
    static inline float max_fader_gain() { return 3.981072f; }  // Scale factor for +12 dB

    /**
     * @brief Maps a decibel value safely within the professional fader boundaries.
     */
    static inline float clamp_dB(float dB)
    {
        return std::clamp(dB, min_fader_dB(), max_fader_dB());
    }

    /**
     * @brief Clamps a linear gain coefficient safely within the professional fader boundaries.
     */
    static inline float clamp_gain(float gain)
    {
        if (gain < min_fader_gain()) return 0.0f;
        if (gain > max_fader_gain()) return max_fader_gain();
        return gain;
    }

    static inline float coefficient_to_dB (float coeff)
    {
        if (coeff < min_fader_gain()) return min_fader_dB();
        return 20.0f * log10 (coeff);
    }

    /**
     * @brief Maps a decibel value to a standardized 0.0 - 1.0 fader position.
     */
    static float db_to_fader_position(float dB);

    /**
     * @brief Maps a standardized 0.0 - 1.0 fader travel position back to a linear audio gain multiplier coefficient.
     */
    static float fader_position_to_gain(float position);
};

#endif
