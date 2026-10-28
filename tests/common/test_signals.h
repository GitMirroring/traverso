// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The tests' signals, one sample at a time, computed in double: silence, a
// sine, an impulse, a ramp and the two full-length sweeps of the decoder
// contract test. A scenario file (.tscenario) describes the same signals;
// its decoder computes them on its own, these are the reference.
// (The windowed chirp of the resampler tests stays in resampling/sweeps.h.)

#include "TTypes.h"

#include <cmath>

struct TestSignals
{
    // Exact in float up to 2^24 frames (5.8 minutes at 48 kHz): the ramp is the
    // frame divided by 2^24. Longer material takes the high part in a second
    // channel (ramp_high()), so every frame stays exact.
    static constexpr double RampFrames = 16777216.0;

    static float ramp(nframes_t frame)
    {
        return float(double(frame % nframes_t(RampFrames)) / RampFrames);
    }

    static float ramp_high(nframes_t frame)
    {
        return float(double(frame / nframes_t(RampFrames)) / RampFrames);
    }

    static float silence(nframes_t)
    {
        return 0.0f;
    }

    static float sine(nframes_t frame, uint rate, double frequency, double amplitude)
    {
        return float(amplitude * std::sin(2.0 * M_PI * frequency * double(frame) / rate));
    }

    static float impulse(nframes_t frame, nframes_t at, double amplitude)
    {
        return frame == at ? float(amplitude) : 0.0f;
    }

    // From start to end Hz over frames frames, the frequency rising (or
    // falling) exponentially
    static float sweep_exponential(nframes_t frame, uint rate, nframes_t frames, double start, double end, double amplitude)
    {
        const double t = double(frame) / rate;
        const double length = double(frames) / rate;
        const double k = std::log(end / start) / length;
        return float(amplitude * std::sin(2.0 * M_PI * start * (std::exp(k * t) - 1.0) / k));
    }

    // From start to end Hz over frames frames, linearly
    static float sweep_linear(nframes_t frame, uint rate, nframes_t frames, double start, double end, double amplitude)
    {
        const double t = double(frame) / rate;
        const double length = double(frames) / rate;
        return float(amplitude * std::sin(2.0 * M_PI * (start * t + (end - start) * t * t / (2.0 * length))));
    }
};
