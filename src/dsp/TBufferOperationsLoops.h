// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The loops of the buffer operations, for TBufferOperations.cpp and
// TBufferOperationsAVX2.cpp: one source, compiled once per file with that
// file's target flags (T20, doc/design/t20-buffer-operations-measurement.md).
//
// Every function here is static: each file gets its own copy, built for its
// own target. No C++ library templates (std::max and the like) either: their
// copies are shared between files, so the linker could hand the AVX2 copy
// to the portable path, which then fails on a CPU without AVX2.

#include "TTypes.h"
#include "TRealTime.h"

#include <cmath>
#if defined(__SSE__)
#include <xmmintrin.h>
#endif

// The peak of buf, or current when that is higher
static inline float buffer_loop_update_peak(const audio_sample_t* buf, nframes_t nframes, float current) T_REALTIME_SAFE
{
    nframes_t i = 0;
#if defined(__GNUC__)
    // GCC and Clang (MinGW too): the maximum in four vectors of four lanes.
    // A maximum in one chain is never vectorised: that would reorder it, which
    // the compiler may not do. Written in vectors, it is vectorised at -O2,
    // whatever the vectoriser decides.
    using Float4 = float __attribute__((vector_size(16)));
    // A load at any float address: no alignment, may alias the samples. A
    // typedef, as the compilers' own __m128_u: Clang drops the aligned(4)
    // of the same attributes in a using alias (an aligned load, a crash)
    typedef float Float4Load __attribute__((vector_size(16), aligned(4), may_alias));
    const auto abs4 = [](Float4 x) T_REALTIME_SAFE {
        for (int lane = 0; lane < 4; ++lane) {
            x[lane] = fabsf(x[lane]);
        }
        return x;
    };
    // a > b ? a : b per lane. On x86 as maxps: GCC makes the vector ternary
    // a compare and three logic operations in a chain (the old SSE assembly
    // was faster on short buffers for it). maxps is the same, NaN and the
    // signed zeros included, so the result is bit for bit the same
    const auto max4 = [](Float4 a, Float4 b) T_REALTIME_SAFE {
#if defined(__SSE__)
        return Float4(_mm_max_ps(__m128(a), __m128(b)));
#else
        return a > b ? a : b;
#endif
    };

    const Float4 start = {current, current, current, current};
    Float4 m0 = start, m1 = start, m2 = start, m3 = start;
    for (; i + 16 <= nframes; i += 16) {
        const Float4Load* p = reinterpret_cast<const Float4Load*>(buf + i);
        m0 = max4(m0, abs4(p[0]));
        m1 = max4(m1, abs4(p[1]));
        m2 = max4(m2, abs4(p[2]));
        m3 = max4(m3, abs4(p[3]));
    }
    const Float4 m = max4(max4(m0, m1), max4(m2, m3));
    for (int lane = 0; lane < 4; ++lane) {
        current = m[lane] > current ? m[lane] : current;
    }
#endif
    for (; i < nframes; ++i) {
        const float sample = fabsf(buf[i]);
        current = sample > current ? sample : current;
    }
    return current;
}

static inline void buffer_loop_apply_gain(audio_sample_t* buf, nframes_t nframes, float gain) T_REALTIME_SAFE
{
    for (nframes_t i = 0; i < nframes; ++i) {
        buf[i] *= gain;
    }
}

// dst and src never overlap (TChannelBuffer::mix_buffers_*(), T64): with
// __restrict the compiler needs no overlap check, which GCC's -O2 cost
// model refuses, and then would not vectorise the loop
static inline void buffer_loop_mix_with_gain(audio_sample_t* __restrict dst, const audio_sample_t* __restrict src, nframes_t nframes, float gain) T_REALTIME_SAFE
{
    for (nframes_t i = 0; i < nframes; ++i) {
        dst[i] += src[i] * gain;
    }
}

static inline void buffer_loop_mix(audio_sample_t* __restrict dst, const audio_sample_t* __restrict src, nframes_t nframes) T_REALTIME_SAFE
{
    for (nframes_t i = 0; i < nframes; ++i) {
        dst[i] += src[i];
    }
}
