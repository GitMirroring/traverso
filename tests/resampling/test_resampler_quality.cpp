// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of the qualities of TAudioResampler: each quality of a
// backend is what the backend means by it. Measured from 48 to 44.1 kHz: a
// tone above the new Nyquist frequency must go (rejection), a tone below it
// must stay (passband). Down the list (best first) neither gets better, and
// the best quality of each backend rejects and passes well. Fed the numbers
// of the other backend, the order breaks. And at the end of the input one
// flush of pending_frames() gives all that is left, at ratios from 1/6 to 8.7.

#include "TAudioResampler.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

static int failures = 0;
// Every resampler of level_after() gave all it held with one flush of
// pending_frames(): a second flush gave nothing
static bool flushedAll = true;

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

    TAudioResampler resampler(backend, double(OutputRate) / InputRate, quality, 1);
    TChannelBuffer in(1024);
    TChannelBuffer out(frames + 4096);
    TChannelBuffer* inBuffer = &in;
    TChannelBuffer* outBuffer = &out;
    nframes_t produced = 0;
    for (nframes_t at = 0; at < frames; at += 1024) {
        const nframes_t n = std::min<nframes_t>(1024, frames - at);
        std::copy_n(input.data() + at, n, in.data(0, n));
        produced += resampler.process(&inBuffer, n, &outBuffer, produced, out.get_size() - produced);
    }
    // One flush of pending_frames() gives all that is left
    const nframes_t pending = resampler.pending_frames();
    if (produced + pending <= out.get_size()) {
        produced += resampler.flush(&outBuffer, produced, pending);
        const nframes_t again = resampler.flush(&outBuffer, produced, std::min<nframes_t>(16, out.get_size() - produced));
        flushedAll = flushedAll && again == 0;
    }
    const float* output = out.data(0, produced);

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

// At the end of the input one flush of pending_frames() gives all that is
// left, and the output is the input's length at the new rate within a
// frame: at every quality of a backend, converting up and down, by small and
// large ratios
static void flush_gives_all(TAudioResampler::BackendType backend, uint inputRate, uint outputRate)
{
    const double ratio = double(outputRate) / inputRate;
    const nframes_t frames = inputRate;
    const QByteArray name = TAudioResampler::backend_name(backend).toLatin1();
    for (const int quality : TAudioResampler::qualities(backend)) {
        TAudioResampler resampler(backend, ratio, quality, 2);
        const nframes_t outSize = nframes_t(std::ceil(frames * ratio)) + 1024 * 10;
        TChannelBuffer in[] = {TChannelBuffer(1024), TChannelBuffer(1024)};
        TChannelBuffer out[] = {TChannelBuffer(outSize), TChannelBuffer(outSize)};
        TChannelBuffer* inBuffers[] = {&in[0], &in[1]};
        TChannelBuffer* outBuffers[] = {&out[0], &out[1]};
        nframes_t produced = 0;
        for (nframes_t at = 0; at < frames; at += 1024) {
            const nframes_t n = std::min<nframes_t>(1024, frames - at);
            for (uint channel = 0; channel < 2; ++channel) {
                float* data = in[channel].data(0, n);
                for (nframes_t i = 0; i < n; ++i) {
                    data[i] = float(0.5 * std::sin(2.0 * M_PI * 440.0 * (at + i) / inputRate + channel));
                }
            }
            produced += resampler.process(inBuffers, n, outBuffers, produced, out[0].get_size() - produced);
        }
        const nframes_t pending = resampler.pending_frames();
        const bool room = produced + pending + 16 <= out[0].get_size();
        nframes_t again = 1;
        if (room) {
            produced += resampler.flush(outBuffers, produced, pending);
            again = resampler.flush(outBuffers, produced, 16);
        }
        const double expected = frames * ratio;
        char what[200];
        std::snprintf(what, sizeof(what), "%s %s, %u -> %u Hz: one flush of pending_frames() (%u) gives all, %u frames of %.1f",
                      name.constData(), TAudioResampler::quality_name(backend, quality).toLatin1().constData(), inputRate, outputRate,
                      pending, produced, expected);
        check(room && again == 0 && std::fabs(double(produced) - expected) <= 1.0, what);
    }
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

    check(flushedAll, "every quality of both backends: one flush of pending_frames() frames gives all it holds");

    struct Rates { uint input; uint output; };
    for (TAudioResampler::BackendType backend : {TAudioResampler::BackendType::LIBSOXR, TAudioResampler::BackendType::LIBSAMPLERATE}) {
        for (const Rates r : {Rates{44100, 48000}, Rates{48000, 44100}, Rates{8000, 48000}, Rates{48000, 8000}, Rates{11025, 64000},
                              Rates{192000, 44100}, Rates{22050, 192000}}) {
            flush_gives_all(backend, r.input, r.output);
        }
    }

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all checks passed\n");
    return 0;
}
