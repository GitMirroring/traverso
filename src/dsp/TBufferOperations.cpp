// SPDX-FileCopyrightText: 2005-2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TBufferOperations.h"
#include "TBufferOperationsLoops.h"
#include "TCpuFeatures.h"
#include "TTypes.h"
#include <QtGlobal>
#include <algorithm>
#if defined(Q_OS_MACOS)
#include <Accelerate/Accelerate.h>
#endif

// The implementations: only this file knows them. The portable ones are the
// loops of TBufferOperationsLoops.h built for the baseline target
static float portable_update_peak(const audio_sample_t* buf, nframes_t nframes, float current) T_REALTIME_SAFE
{
    return buffer_loop_update_peak<4>(buf, nframes, current);
}

static void portable_apply_gain(audio_sample_t* buf, nframes_t nframes, float gain) T_REALTIME_SAFE
{
    buffer_loop_apply_gain(buf, nframes, gain);
}

static void portable_mix_with_gain(audio_sample_t* dst, const audio_sample_t* src, nframes_t nframes, float gain) T_REALTIME_SAFE
{
    buffer_loop_mix_with_gain(dst, src, nframes, gain);
}

static void portable_mix(audio_sample_t* dst, const audio_sample_t* src, nframes_t nframes) T_REALTIME_SAFE
{
    buffer_loop_mix(dst, src, nframes);
}

static const TBufferOperations::Implementation s_portable{
    "portable", portable_update_peak, portable_apply_gain, portable_mix_with_gain, portable_mix};

#if defined(TRAVERSO_AVX2_OPERATIONS)
// The same loops built for AVX2 (TBufferOperationsAVX2.cpp)
float avx2_update_peak(const audio_sample_t* buf, nframes_t nframes, float current) T_REALTIME_SAFE;
void  avx2_apply_gain(audio_sample_t* buf, nframes_t nframes, float gain) T_REALTIME_SAFE;
void  avx2_mix_with_gain(audio_sample_t* dst, const audio_sample_t* src, nframes_t nframes, float gain) T_REALTIME_SAFE;
void  avx2_mix(audio_sample_t* dst, const audio_sample_t* src, nframes_t nframes) T_REALTIME_SAFE;

static const TBufferOperations::Implementation s_avx2{
    "AVX2", avx2_update_peak, avx2_apply_gain, avx2_mix_with_gain, avx2_mix};
#endif

#if defined (Q_OS_MACOS)
static float accel_compute_peak              (const audio_sample_t* buf, nframes_t nsamples, float current) T_REALTIME_SAFE;
static void  accel_apply_gain_to_buffer      (audio_sample_t* buf, nframes_t nframes, float gain) T_REALTIME_SAFE;
static void  accel_mix_buffers_with_gain     (audio_sample_t* dst, const audio_sample_t* src, nframes_t nframes, float gain) T_REALTIME_SAFE;
static void  accel_mix_buffers_no_gain       (audio_sample_t* dst, const audio_sample_t* src, nframes_t nframes) T_REALTIME_SAFE;

static const TBufferOperations::Implementation s_accelerate{
    "Apple Accelerate", accel_compute_peak, accel_apply_gain_to_buffer, accel_mix_buffers_with_gain, accel_mix_buffers_no_gain};
#endif

// The portable versions from the start (constant initialization, before any
// code runs), so the operations work without the application; the best
// implementation for this machine replaces them below, still before main()
constinit TBufferOperations::UpdatePeak  TBufferOperations::update_peak   = portable_update_peak;
constinit TBufferOperations::ApplyGain   TBufferOperations::apply_gain    = portable_apply_gain;
constinit TBufferOperations::MixWithGain TBufferOperations::mix_with_gain = portable_mix_with_gain;
constinit TBufferOperations::Mix         TBufferOperations::mix           = portable_mix;
constinit const char* TBufferOperations::s_implementation = "portable";

std::vector<TBufferOperations::Implementation> TBufferOperations::available_implementations()
{
    std::vector<Implementation> implementations{s_portable};
#if defined(Q_OS_MACOS)
    implementations.push_back(s_accelerate);
#elif defined(TRAVERSO_AVX2_OPERATIONS)
    if (TCpuFeatures::detect().avx2) {
        implementations.push_back(s_avx2);
    }
#endif
    return implementations;
}

// Before main(), so before any thread: no thread sees the pointers change.
// Not on first use, which can be the audio thread (cpuid, a race on the
// pointers). No log here: TLog may not exist yet (implementation_name()).
bool TBufferOperations::choose_best_implementation()
{
    const Implementation best = available_implementations().back();
    update_peak   = best.updatePeak;
    apply_gain    = best.applyGain;
    mix_with_gain = best.mixWithGain;
    mix           = best.mix;
    s_implementation = best.name;
    return true;
}

const bool TBufferOperations::s_bestImplementationChosen = choose_best_implementation();


#if defined (Q_OS_MACOS)

float accel_compute_peak (const audio_sample_t* buf, nframes_t nsamples, float current) T_REALTIME_SAFE
{
	float tmpmax = 0.0f;
	// Accelerate's vDSP: real-time safe by contract
	T_REALTIME_SAFE_BY_CONTRACT(vDSP_maxmgv(buf, 1, &tmpmax, nsamples);)
	return std::max(current, tmpmax);
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
