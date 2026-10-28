// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of TBufferOperations without the application (tasks T52,
// T20): the operations work as they are (no init call), the best one this
// machine runs is in use, and every implementation it runs (portable, AVX2,
// Apple Accelerate) gives what plain scalar loops here give, on random
// samples of many lengths, also at pointers that are not 16-byte aligned.
// The reference is the test's own, never one of the implementations. Peaks
// exactly; mixes and gain exactly on x86 (no FMA there: every operation is
// the same IEEE operation per sample), elsewhere within 1e-6 (an FMA may
// join d + s * g into one rounding).

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

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
static constexpr float Tolerance = 0.0f;
#else
static constexpr float Tolerance = 1.0e-6f;
#endif

// The largest difference between two runs of nframes samples
static float worst(const float* a, const float* b, nframes_t nframes)
{
    float result = 0.0f;
    for (nframes_t i = 0; i < nframes; ++i) {
        result = std::max(result, std::fabs(a[i] - b[i]));
    }
    return result;
}

// One implementation against the reference
static void test_implementation(const TBufferOperations::Implementation& implementation)
{
    std::mt19937 random(2026);
    std::uniform_real_distribution<float> sample(-1.0f, 1.0f);
    const nframes_t lengths[] = {0, 1, 2, 3, 4, 5, 7, 8, 15, 16, 17, 31, 32, 33, 63, 64, 65, 255, 1000, 1027, 4096};
    bool peaks = true;
    bool gains = true;
    bool mixes = true;
    bool mixesWithGain = true;
    char what[200];
    for (const nframes_t nframes : lengths) {
        // Offsets 0 to 3 floats from a 16-byte boundary, for dst and src
        // apart
        for (size_t combination = 0; combination < 16; ++combination) {
            const size_t offset = combination % 4;
            const size_t srcOffset = combination / 4;
            alignas(16) static float srcStore[4096 + 8];
            alignas(16) static float a[4096 + 8];
            alignas(16) static float b[4096 + 8];
            float* src = srcStore + srcOffset;
            float* dstA = a + offset;
            float* dstB = b + offset;
            for (nframes_t i = 0; i < nframes; ++i) {
                src[i] = sample(random);
                dstA[i] = dstB[i] = sample(random);
            }
            // A guard sample after the run: nothing may write it
            dstA[nframes] = dstB[nframes] = 42.0f;

            const float current = 0.125f;
            if (implementation.updatePeak(src, nframes, current) != reference_peak(src, nframes, current)) {
                std::snprintf(what, sizeof(what), "%s: peak differs at %u frames, offsets %zu and %zu", implementation.name, nframes, offset, srcOffset);
                check(false, what);
                peaks = false;
            }

            implementation.applyGain(dstA, nframes, 0.7f);
            reference_gain(dstB, nframes, 0.7f);
            gains = gains && worst(dstA, dstB, nframes) <= Tolerance && dstA[nframes] == 42.0f;

            implementation.mix(dstA, src, nframes);
            reference_mix(dstB, src, nframes);
            mixes = mixes && worst(dstA, dstB, nframes) <= Tolerance && dstA[nframes] == 42.0f;

            implementation.mixWithGain(dstA, src, nframes, -0.3f);
            reference_mix_with_gain(dstB, src, nframes, -0.3f);
            mixesWithGain = mixesWithGain && worst(dstA, dstB, nframes) <= Tolerance && dstA[nframes] == 42.0f;
        }
    }
    std::snprintf(what, sizeof(what), "%s: update_peak exactly as the reference, every length and alignment", implementation.name);
    check(peaks, what);
    std::snprintf(what, sizeof(what), "%s: apply_gain as the reference (exactly on x86), nothing written past the end", implementation.name);
    check(gains, what);
    std::snprintf(what, sizeof(what), "%s: mix as the reference (exactly on x86), nothing written past the end", implementation.name);
    check(mixes, what);
    std::snprintf(what, sizeof(what), "%s: mix_with_gain as the reference (exactly on x86), nothing written past the end", implementation.name);
    check(mixesWithGain, what);
}

int main()
{
    std::printf("implementation: %s\n", TBufferOperations::implementation_name());
    const std::vector<TBufferOperations::Implementation> implementations = TBufferOperations::available_implementations();
    check(std::strcmp(implementations.front().name, "portable") == 0, "the portable implementation is always there");
    check(std::strcmp(TBufferOperations::implementation_name(), implementations.back().name) == 0,
          "the best implementation this machine runs is in use");

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

    for (const TBufferOperations::Implementation& implementation : implementations) {
        test_implementation(implementation);
    }

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all buffer operation tests passed\n");
    return 0;
}
