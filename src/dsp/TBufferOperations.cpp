// SPDX-FileCopyrightText: 2005-2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TBufferOperations.h"
#include "TCpuFeatures.h"
#include "TTypes.h"
#include <QtGlobal>
#include <cmath> // used for fabs
#if defined(Q_OS_MACOS)
#include <Accelerate/Accelerate.h>
#endif

// The implementations: only this file knows them
static inline float f_max(float x, float a)
{
    x -= a;
    x += fabsf (x);
    x *= 0.5f;
    x += a;
    return (x);
}

static float portable_update_peak    (const audio_sample_t*  buf, nframes_t nframes, float current) T_REALTIME_SAFE;
static void  portable_apply_gain     (audio_sample_t*  buf, nframes_t nframes, float gain) T_REALTIME_SAFE;
static void  portable_mix_with_gain  (audio_sample_t*  dst, const audio_sample_t*  src, nframes_t nframes, float gain) T_REALTIME_SAFE;
static void  portable_mix            (audio_sample_t*  dst, const audio_sample_t*  src, nframes_t nframes) T_REALTIME_SAFE;

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
static float accel_compute_peak              (const audio_sample_t* buf, nframes_t nsamples, float current) T_REALTIME_SAFE;
static void  accel_apply_gain_to_buffer      (audio_sample_t* buf, nframes_t nframes, float gain) T_REALTIME_SAFE;
static void  accel_mix_buffers_with_gain     (audio_sample_t* dst, const audio_sample_t* src, nframes_t nframes, float gain) T_REALTIME_SAFE;
static void  accel_mix_buffers_no_gain       (audio_sample_t* dst, const audio_sample_t* src, nframes_t nframes) T_REALTIME_SAFE;
#endif

// The portable versions from the start (constant initialization, before any
// code runs), so the operations work without the application; the best
// implementation for this machine replaces them below, still before main()
constinit TBufferOperations::UpdatePeak  TBufferOperations::update_peak   = portable_update_peak;
constinit TBufferOperations::ApplyGain   TBufferOperations::apply_gain    = portable_apply_gain;
constinit TBufferOperations::MixWithGain TBufferOperations::mix_with_gain = portable_mix_with_gain;
constinit TBufferOperations::Mix         TBufferOperations::mix           = portable_mix;
constinit const char* TBufferOperations::s_implementation = "portable";

// Before main(), so before any thread: no thread sees the pointers change.
// Not on first use, which can be the audio thread (cpuid, a race on the
// pointers). No log here: TLog may not exist yet (implementation_name()).
bool TBufferOperations::choose_best_implementation()
{
#if defined(__SSE__) && defined(SSE_OPTIMIZATIONS)
    if (TCpuFeatures::detect().sse) {
        update_peak   = x86_sse_compute_peak;
        apply_gain    = x86_sse_apply_gain_to_buffer;
        mix_with_gain = x86_sse_mix_buffers_with_gain;
        mix           = x86_sse_mix_buffers_no_gain;
        s_implementation = "SSE";
    }
#elif defined(Q_OS_MACOS)
    update_peak   = accel_compute_peak;
    apply_gain    = accel_apply_gain_to_buffer;
    mix_with_gain = accel_mix_buffers_with_gain;
    mix           = accel_mix_buffers_no_gain;
    s_implementation = "Apple Accelerate";
#endif
    return true;
}

const bool TBufferOperations::s_bestImplementationChosen = choose_best_implementation();

float portable_update_peak (const audio_sample_t* buf, nframes_t nframes, float current) T_REALTIME_SAFE
{
        for (nframes_t i = 0; i < nframes; ++i) {
                current = f_max (current, fabsf (buf[i]));
        }

        return current;
}

void portable_apply_gain (audio_sample_t* buf, nframes_t nframes, float gain) T_REALTIME_SAFE
{
        for (nframes_t i=0; i<nframes; i++)
                buf[i] *= gain;
}

void portable_mix_with_gain (audio_sample_t* dst, const audio_sample_t* src, nframes_t nframes, float gain) T_REALTIME_SAFE
{
        for (nframes_t i = 0; i < nframes; i++) {
                dst[i] += src[i] * gain;
        }
}

void portable_mix (audio_sample_t* dst, const audio_sample_t* src, nframes_t nframes) T_REALTIME_SAFE
{
        for (nframes_t i=0; i < nframes; i++) {
                dst[i] += src[i];
        }
}


#if defined (Q_OS_MACOS)

float accel_compute_peak (const audio_sample_t* buf, nframes_t nsamples, float current) T_REALTIME_SAFE
{
	float tmpmax = 0.0f;
	// Accelerate's vDSP: real-time safe by contract
	T_REALTIME_SAFE_BY_CONTRACT(vDSP_maxmgv(buf, 1, &tmpmax, nsamples);)
	return f_max(current, tmpmax);
}

void accel_apply_gain_to_buffer (audio_sample_t * buf, nframes_t nframes, float gain) T_REALTIME_SAFE
{
	T_REALTIME_SAFE_BY_CONTRACT(vDSP_vsmul(buf, 1, &gain, buf, 1, nframes);)
}

void accel_mix_buffers_with_gain (audio_sample_t * dst, const audio_sample_t * src, nframes_t nframes, float gain) T_REALTIME_SAFE
{
	T_REALTIME_SAFE_BY_CONTRACT(vDSP_vsma(src, 1, &gain, dst, 1, dst, 1, nframes);)
}

void accel_mix_buffers_no_gain (audio_sample_t * dst, const audio_sample_t * src, nframes_t nframes) T_REALTIME_SAFE
{
	T_REALTIME_SAFE_BY_CONTRACT(vDSP_vadd(src, 1, dst, 1, dst, 1, nframes);)
}

#endif
