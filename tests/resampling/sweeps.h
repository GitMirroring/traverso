// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The sweeps the resampling tests read and write, and where and how whole
// they are found in the output: a short sweep is placed on a known frame
// and located afterwards by correlation, to a fraction of a frame.

#include <algorithm>
#include <cmath>
#include <vector>

// A sweep from 500 Hz to high in 20 ms with a Hann window
static constexpr double SweepSeconds = 0.020;

static std::vector<float> sweep(uint rate, double high = 6000.0)
{
    const int n = int(std::lround(SweepSeconds * rate));
    std::vector<float> s(size_t(n), 0.0f);
    const double k = (high - 500.0) / SweepSeconds;
    for (int i = 0; i < n; ++i) {
        const double t = double(i) / rate;
        const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / (n - 1));
        s[size_t(i)] = float(0.5 * w * std::sin(2.0 * M_PI * (500.0 * t + 0.5 * k * t * t)));
    }
    return s;
}

// Where the sweep that belongs at expected (a frame of out) is, against
// expected: the peak of the correlation with the sweep within search frames
// either way, interpolated. The window stops at the end of out (the last
// sweep may be close to it); NAN when nothing of it is left.
static double sweep_offset(const std::vector<float>& out, double expected, const std::vector<float>& tpl, int search)
{
    const int start = std::max(1, int(expected) - search);
    const int end = std::min(int(expected) + search, int(out.size()) - int(tpl.size()) - 2);
    if (end <= start) {
        return NAN;
    }
    std::vector<double> corr(size_t(end - start + 1), 0.0);
    for (int lag = start; lag <= end; ++lag) {
        double sum = 0.0;
        for (size_t i = 0; i < tpl.size(); ++i) {
            sum += double(out[size_t(lag) + i]) * tpl[i];
        }
        corr[size_t(lag - start)] = sum;
    }
    size_t best = 0;
    for (size_t i = 1; i < corr.size(); ++i) {
        if (std::fabs(corr[i]) > std::fabs(corr[best])) {
            best = i;
        }
    }
    double frac = 0.0;
    if (best > 0 && best + 1 < corr.size()) {
        const double y0 = corr[best - 1], y1 = corr[best], y2 = corr[best + 1];
        const double d = y0 - 2.0 * y1 + y2;
        frac = d != 0.0 ? 0.5 * (y0 - y2) / d : 0.0;
    }
    return start + double(best) + frac - expected;
}

// The level of the sweep at frame at against the sweep itself: 1 when it is
// there as it was written, less when it is cut short or attenuated
static double sweep_level(const std::vector<float>& out, size_t at, const std::vector<float>& tpl)
{
    double match = 0.0, energy = 0.0;
    for (size_t i = 0; i < tpl.size() && at + i < out.size(); ++i) {
        match += double(out[at + i]) * tpl[i];
        energy += double(tpl[i]) * tpl[i];
    }
    return match / energy;
}
