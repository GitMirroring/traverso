// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of TResampleAudioReader: a file of short sweeps, read at
// another rate the way the stream reader reads it, must come out with every
// sweep where it belongs on the file's timeline, within a twentieth of a frame. A
// reader that pads a short read of the resampler with silence fails it: the
// silence shifts all that follows.

#include "ResampleAudioReader.h"
#include "TFileIOBuffer.h"
#include "TTimeRef.h"

#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

static int failures = 0;

static void check(bool ok, const char* what)
{
    if (!ok) {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}

// A sweep from 500 Hz to high in 20 ms with a Hann window, one per second
static constexpr double SweepSeconds = 0.020;
static constexpr int SweepCount = 12;

static std::vector<float> sweep(uint rate, double high)
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

static double sweep_start(int k)
{
    return 1.0 + k;
}

// Mono 32 bit float WAV
static bool write_wav(const QString& path, uint rate, const std::vector<float>& samples)
{
    FILE* f = std::fopen(path.toLocal8Bit().constData(), "wb");
    if (!f) {
        return false;
    }
    const uint32_t dataBytes = uint32_t(samples.size() * sizeof(float));
    auto u32 = [f](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [f](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f);
    u32(4 + 8 + 16 + 8 + dataBytes);
    std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16);
    u16(3);              // IEEE float
    u16(1);
    u32(rate);
    u32(rate * 4);
    u16(4);
    u16(32);
    std::fwrite("data", 1, 4, f);
    u32(dataBytes);
    std::fwrite(samples.data(), sizeof(float), samples.size(), f);
    std::fclose(f);
    return true;
}

// Where the sweep that belongs at expected (a frame of out) is, against
// expected: the peak of the correlation with the sweep, interpolated
static double sweep_offset(const std::vector<float>& out, double expected, const std::vector<float>& tpl, int search)
{
    const int start = int(expected) - search;
    const int end = int(expected) + search;
    if (start < 1 || end + int(tpl.size()) + 1 >= int(out.size())) {
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

enum class Pattern { Buffers, Stream, Seek };

static const char* pattern_name(Pattern p)
{
    return p == Pattern::Buffers ? "buffers" : p == Pattern::Stream ? "stream" : "seek";
}

// Reads the whole file at outputRate as TBufferedAudioStreamReader does:
// Buffers, one buffer per read; Stream, after a seek 70% of a ring of
// rate / buffer slots, then all free slots once the ring is at 80% or less;
// Seek, the stream pattern from 3.3 s on. Returns the output and its first
// frame on the file's timeline.
static std::vector<float> read_all(const QString& path, uint outputRate, int quality, Pattern pattern, nframes_t& first)
{
    TResampleAudioReader reader(path);
    reader.set_output_rate(outputRate);
    reader.set_converter_type(quality);

    const nframes_t bufferSize = 1024;
    const nframes_t slotCount = std::max<nframes_t>(5, outputRate / bufferSize);
    const nframes_t total = nframes_t(double(reader.get_nframes()) / bufferSize);
    first = pattern == Pattern::Seek ? nframes_t(3.3 * outputRate) / bufferSize * bufferSize : 0;

    std::vector<float> out;
    TFileIOBuffer buffer;
    TTimeRef location(first, outputRate);
    nframes_t queued = 0;
    nframes_t produced = first / bufferSize;
    bool seeked = true;
    while (produced < total) {
        const nframes_t freeSlots = slotCount - queued;
        nframes_t count = 0;
        if (pattern == Pattern::Buffers) {
            count = queued == 0 ? 1 : 0;
        } else if (seeked) {
            count = nframes_t(0.7 * slotCount);
        } else if (100 - freeSlots * 100 / slotCount <= 80) {
            count = freeSlots;
        }
        if (count) {
            const nframes_t frames = count * bufferSize;
            buffer.check_capacity(frames, 1);
            buffer.silence_buffers();
            reader.read_from(buffer, location, frames);
            const float* data = buffer.get_channel_buffer(0).get_data(frames);
            out.insert(out.end(), data, data + frames);
            location += TTimeRef(frames, outputRate);
            queued += count;
            seeked = false;
        }
        if (queued) {
            --queued;
            ++produced;
        }
    }
    return out;
}

int main()
{
    QTemporaryDir dir;
    check(dir.isValid(), "a temporary directory");

    // The common pairs, the lowest and highest rates, the largest drop
    // after a seek (11025 -> 64000: 2559 frames), and equal rates, which
    // read the file as it is
    struct Case { uint fileRate; uint outputRate; };
    const Case cases[] = {{44100, 48000}, {48000, 44100}, {96000, 48000}, {22050, 48000}, {44100, 96000},
                          {88200, 48000}, {8000, 48000}, {192000, 44100}, {11025, 64000}, {48000, 48000}};
    // libsamplerate's numbers, as the settings pass them: best, fastest, linear
    const int qualities[] = {0, 2, 4};

    for (const Case& c : cases) {
        // Below both Nyquist frequencies, so the sweep is the same at either rate
        const double high = std::min(6000.0, 0.4 * std::min(c.fileRate, c.outputRate));
        const std::vector<float> s = sweep(c.fileRate, high);
        std::vector<float> file(size_t(std::lround((sweep_start(SweepCount - 1) + 1.5) * c.fileRate)), 0.0f);
        for (int k = 0; k < SweepCount; ++k) {
            std::copy(s.begin(), s.end(), file.begin() + long(std::lround(sweep_start(k) * c.fileRate)));
        }
        const QString path = dir.filePath(QString("sweeps-%1.wav").arg(c.fileRate));
        check(write_wav(path, c.fileRate, file), "writing the sweeps");

        const std::vector<float> tpl = sweep(c.outputRate, high);
        for (int quality : qualities) {
            for (Pattern pattern : {Pattern::Buffers, Pattern::Stream, Pattern::Seek}) {
                nframes_t first = 0;
                const std::vector<float> out = read_all(path, c.outputRate, quality, pattern, first);
                double worst = 0.0;
                int found = 0;
                for (int k = 0; k < SweepCount; ++k) {
                    const double expected = sweep_start(k) * c.outputRate - first;
                    if (expected < 2000) {
                        continue;
                    }
                    const double offset = sweep_offset(out, expected, tpl, 2000);
                    if (std::isnan(offset)) {
                        continue;
                    }
                    ++found;
                    if (std::fabs(offset) > std::fabs(worst)) {
                        worst = offset;
                    }
                }
                char what[200];
                std::snprintf(what, sizeof(what), "%u -> %u Hz, quality %d, %s: every sweep in place (worst %+.2f frames, %d sweeps)",
                              c.fileRate, c.outputRate, quality, pattern_name(pattern), worst, found);
                const bool ok = found >= SweepCount - 4 && std::fabs(worst) <= 0.05;
                check(ok, what);
                if (ok) {
                    std::printf("ok:   %s\n", what);
                }
            }
        }
    }

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all checks passed\n");
    return 0;
}
