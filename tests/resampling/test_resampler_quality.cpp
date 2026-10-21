// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of the qualities of TAudioResampler: each quality of a
// backend is what the backend means by it. Measured from 48 to 44.1 kHz: a
// tone above the new Nyquist frequency must go (rejection), a tone below it
// must stay (passband). Down the list (best first) neither gets better, and
// the best quality of each backend rejects and passes well. Fed the numbers
// of the other backend, the order breaks.

#include "TAudioResampler.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

static int failures = 0;

static void check(bool ok, const char* what)
{
    if (!ok) {
        ++failures;
        std::printf("FAIL: %s\n", what);
    } else {
        std::printf("ok:   %s\n", what);
    }
}

static constexpr uint InputRate = 48000;
static constexpr uint OutputRate = 44100;

// The level of a tone of frequency after resampling, in dB against its
// level before
static double level_after(TAudioResampler::BackendType backend, int quality, double frequency)
{
    const nframes_t frames = InputRate;
    std::vector<float> input(frames);
    for (nframes_t i = 0; i < frames; ++i) {
        input[i] = float(0.5 * std::sin(2.0 * M_PI * frequency * i / InputRate));
    }

    TAudioResampler resampler(backend, double(OutputRate) / InputRate, quality, 1024);
    std::vector<float> output(frames + 4096, 0.0f);
    nframes_t produced = 0;
    for (nframes_t at = 0; at < frames; at += 1024) {
        const nframes_t n = std::min<nframes_t>(1024, frames - at);
        produced += resampler.process(input.data() + at, n, output.data() + produced, nframes_t(output.size()) - produced, false);
    }

    // The middle, away from the start and the end of the tone
    const nframes_t from = OutputRate / 10;
    const nframes_t to = std::min<nframes_t>(produced, OutputRate - OutputRate / 10);
    double sum = 0.0;
    for (nframes_t i = from; i < to; ++i) {
        sum += double(output[i]) * output[i];
    }
    const double rms = std::sqrt(sum / double(to - from));
    return 20.0 * std::log10(std::max(rms, 1e-12) / (0.5 / std::sqrt(2.0)));
}

int main()
{
    for (TAudioResampler::BackendType backend : {TAudioResampler::BackendType::LIBSOXR, TAudioResampler::BackendType::LIBSAMPLERATE}) {
        const QList<int> qualities = TAudioResampler::qualities(backend);
        const QByteArray name = TAudioResampler::backend_name(backend).toLatin1();
        char what[200];

        double previous = 0.0;
        for (int i = 0; i < qualities.size(); ++i) {
            const int quality = qualities[i];
            const double rejection = -level_after(backend, quality, 23000.0);
            const double passband = level_after(backend, quality, 10000.0);
            const QByteArray qualityName = TAudioResampler::quality_name(backend, quality).toLatin1();
            std::snprintf(what, sizeof(what), "%s %s: rejects 23 kHz by %.1f dB, %s", name.constData(), qualityName.constData(),
                          rejection, i == 0 ? "the best" : "less than the one before");
            check(i == 0 ? rejection > 140.0 : rejection < previous, what);
            std::snprintf(what, sizeof(what), "%s %s: passes 10 kHz (%+.3f dB)", name.constData(), qualityName.constData(), passband);
            check(std::fabs(passband) < 0.5, what);
            previous = rejection;
        }

        const double top = level_after(backend, qualities.first(), 20000.0);
        std::snprintf(what, sizeof(what), "%s: the best quality passes 20 kHz (%+.3f dB)", name.constData(), top);
        check(std::fabs(top) < 0.1, what);
        std::snprintf(what, sizeof(what), "%s: the defaults are its own qualities", name.constData());
        check(qualities.contains(TAudioResampler::default_quality(backend)) &&
              qualities.contains(TAudioResampler::default_realtime_quality(backend)), what);
        std::snprintf(what, sizeof(what), "%s: its settings name", name.constData());
        check(TAudioResampler::backend_from_name(TAudioResampler::backend_name(backend)) == backend, what);
    }

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all checks passed\n");
    return 0;
}
