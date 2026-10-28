// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of TBufferOperations without the application (task T52):
// the operations work as they are (no init call), and the implementation in
// use (portable, SSE, Apple Accelerate) gives what plain scalar loops here
// give, on random samples of many lengths, also at pointers that are not
// 16-byte aligned. The reference is the test's own: whatever dsp picks or
// clones (T20: AVX2 through target_clones), it is never compared with itself.
// Within 1e-6: an FMA skips a rounding in a mix, and the portable peak
// (a branch-free max) can round its last bit.

#include "TBufferOperations.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

static int failures = 0;

static void check(bool ok, const char* what)
{
    std::printf("%s: %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) {
        ++failures;
    }
}

// The reference: plain loops, never vectorised by dsp's choice
static float reference_peak(const float* buf, nframes_t nframes, float current)
{
    for (nframes_t i = 0; i < nframes; ++i) {
        current = std::max(current, std::fabs(buf[i]));
    }
    return current;
}

static void reference_gain(float* buf, nframes_t nframes, float gain)
{
    for (nframes_t i = 0; i < nframes; ++i) {
        buf[i] *= gain;
    }
}

static void reference_mix_with_gain(float* dst, const float* src, nframes_t nframes, float gain)
{
    for (nframes_t i = 0; i < nframes; ++i) {
        dst[i] += src[i] * gain;
    }
}

static void reference_mix(float* dst, const float* src, nframes_t nframes)
{
    for (nframes_t i = 0; i < nframes; ++i) {
        dst[i] += src[i];
    }
}

// The largest difference between two runs of nframes samples
static float worst(const float* a, const float* b, nframes_t nframes)
{
    float result = 0.0f;
    for (nframes_t i = 0; i < nframes; ++i) {
        result = std::max(result, std::fabs(a[i] - b[i]));
    }
    return result;
}

int main()
{
    std::printf("implementation: %s\n", TBufferOperations::implementation_name());
#if defined(__SSE__) && defined(SSE_OPTIMIZATIONS)
    check(std::strcmp(TBufferOperations::implementation_name(), "SSE") == 0, "an SSE build uses the SSE operations");
#endif

    // Without any init: a mix, a gain and a peak
    {
        float dst[4] = {0.5f, -0.25f, 0.0f, 1.0f};
        const float src[4] = {0.25f, 0.25f, -0.5f, -1.0f};
        TBufferOperations::mix(dst, src, 4);
        TBufferOperations::apply_gain(dst, 4, 2.0f);
        TBufferOperations::mix_with_gain(dst, src, 4, 0.5f);
        const float peak = TBufferOperations::update_peak(dst, 4, 0.0f);
        const bool right = dst[0] == 1.625f && dst[1] == 0.125f && dst[2] == -1.25f && dst[3] == -0.5f && peak == 1.625f;
        check(right, "mix, gain and peak work without the application");
        check(TBufferOperations::update_peak(dst, 4, 3.0f) == 3.0f, "a higher current peak stays");
    }

    // The implementation in use against the reference
    std::mt19937 random(2026);
    std::uniform_real_distribution<float> sample(-1.0f, 1.0f);
    const nframes_t lengths[] = {0, 1, 2, 3, 4, 5, 7, 8, 15, 16, 17, 31, 63, 64, 65, 255, 1000, 1027, 4096};
    bool peaks = true;
    bool gains = true;
    bool mixes = true;
    bool mixesWithGain = true;
    char what[200];
    for (const nframes_t nframes : lengths) {
        // Offsets 0 to 3 floats from a 16-byte boundary
        for (size_t offset = 0; offset < 4; ++offset) {
            alignas(16) static float srcStore[4096 + 8];
            alignas(16) static float a[4096 + 8];
            alignas(16) static float b[4096 + 8];
            float* src = srcStore + offset;
            float* dstA = a + offset;
            float* dstB = b + offset;
            for (nframes_t i = 0; i < nframes; ++i) {
                src[i] = sample(random);
                dstA[i] = dstB[i] = sample(random);
            }
            // A guard sample after the run: nothing may write it
            dstA[nframes] = dstB[nframes] = 42.0f;

            const float current = 0.125f;
            if (std::fabs(TBufferOperations::update_peak(src, nframes, current) - reference_peak(src, nframes, current)) > 1.0e-6f) {
                std::snprintf(what, sizeof(what), "peak differs at %u frames, offset %zu", nframes, offset);
                check(false, what);
                peaks = false;
            }

            TBufferOperations::apply_gain(dstA, nframes, 0.7f);
            reference_gain(dstB, nframes, 0.7f);
            gains = gains && worst(dstA, dstB, nframes) <= 1.0e-6f && dstA[nframes] == 42.0f;

            TBufferOperations::mix(dstA, src, nframes);
            reference_mix(dstB, src, nframes);
            mixes = mixes && worst(dstA, dstB, nframes) <= 1.0e-6f && dstA[nframes] == 42.0f;

            TBufferOperations::mix_with_gain(dstA, src, nframes, -0.3f);
            reference_mix_with_gain(dstB, src, nframes, -0.3f);
            mixesWithGain = mixesWithGain && worst(dstA, dstB, nframes) <= 1.0e-6f && dstA[nframes] == 42.0f;
        }
    }
    check(peaks, "update_peak as the reference, every length and alignment");
    check(gains, "apply_gain as the reference, nothing written past the end");
    check(mixes, "mix as the reference, nothing written past the end");
    check(mixesWithGain, "mix_with_gain as the reference, nothing written past the end");

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all buffer operation tests passed\n");
    return 0;
}
