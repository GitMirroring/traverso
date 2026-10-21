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
#include <numeric>
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

enum class Pattern { Buffers, Stream, Seek, SeekCommon };

static const char* pattern_name(Pattern p)
{
    switch (p) {
    case Pattern::Buffers: return "buffers";
    case Pattern::Stream: return "stream";
    case Pattern::Seek: return "seek";
    default: return "seek to a common frame";
    }
}

// Reads the whole file at outputRate as TBufferedAudioStreamReader does:
// Buffers, one buffer per read; Stream, after a seek 70% of a ring of
// rate / buffer slots, then all free slots once the ring is at 80% or less;
// Seek, the stream pattern from 3.3 s on; SeekCommon, from 3 s on, a frame
// of both rates (nothing to drop). Returns the output and its first frame
// on the file's timeline.
static std::vector<float> read_all(const QString& path, uint outputRate, int quality, Pattern pattern, nframes_t& first,
                                   nframes_t fileFrames, uint fileRate)
{
    TResampleAudioReader reader(path);
    reader.set_output_rate(outputRate);
    reader.set_converter_type(quality);

    // The length and the frames are the file's, at any output rate; the
    // frames at the output rate end where the file ends
    check(reader.get_length() == TTimeRef(fileFrames, fileRate), "the length is the file's");
    check(reader.get_file_frames() == fileFrames, "the frames of the file at its own rate");
    check(qint64(reader.get_nframes()) == TTimeRef::frame_at_or_after(TTimeRef(fileFrames, fileRate), outputRate),
          "the frames at the output rate end where the file ends");

    const nframes_t bufferSize = 1024;
    const nframes_t slotCount = std::max<nframes_t>(5, outputRate / bufferSize);
    const nframes_t total = nframes_t(double(reader.get_nframes()) / bufferSize);
    first = pattern == Pattern::Seek ? nframes_t(3.3 * outputRate) / bufferSize * bufferSize
          : pattern == Pattern::SeekCommon ? 3 * outputRate : 0;

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
        // 7 frames past a whole half second: the end falls between two
        // frames of the output rate, so a length taken from the output
        // frames would be off
        std::vector<float> file(size_t(std::lround((sweep_start(SweepCount - 1) + 1.5) * c.fileRate)) + 7, 0.0f);
        for (int k = 0; k < SweepCount; ++k) {
            std::copy(s.begin(), s.end(), file.begin() + long(std::lround(sweep_start(k) * c.fileRate)));
        }
        const QString path = dir.filePath(QString("sweeps-%1.wav").arg(c.fileRate));
        check(write_wav(path, c.fileRate, file), "writing the sweeps");

        const std::vector<float> tpl = sweep(c.outputRate, high);
        for (int quality : qualities) {
            for (Pattern pattern : {Pattern::Buffers, Pattern::Stream, Pattern::Seek, Pattern::SeekCommon}) {
                nframes_t first = 0;
                const std::vector<float> out = read_all(path, c.outputRate, quality, pattern, first, nframes_t(file.size()), c.fileRate);
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

    // The end of the file: a sweep that ends 3 ms before it is whole in the
    // output, through the flush of the resampler, read in one go or in
    // pieces of 100 frames (the flush goes on over several reads)
    for (const nframes_t piece : {nframes_t(0), nframes_t(100)}) {
    for (const Case& c : {Case{44100, 48000}, Case{48000, 44100}, Case{96000, 48000}}) {
        const std::vector<float> s = sweep(c.fileRate, 6000.0);
        const nframes_t fileFrames = nframes_t(std::lround((1.0 + SweepSeconds + 0.003) * c.fileRate));
        std::vector<float> file(fileFrames, 0.0f);
        std::copy(s.begin(), s.end(), file.begin() + long(c.fileRate));
        const QString path = dir.filePath(QString("end-%1.wav").arg(c.fileRate));
        check(write_wav(path, c.fileRate, file), "writing the file");

        TResampleAudioReader reader(path);
        reader.set_output_rate(c.outputRate);
        reader.set_converter_type(2);
        const nframes_t frames = reader.get_nframes();
        TFileIOBuffer buffer;
        std::vector<float> out;
        for (nframes_t at = 0; at < frames;) {
            const nframes_t n = piece ? std::min(piece, frames - at) : frames;
            buffer.check_capacity(n, 1);
            buffer.silence_buffers();
            reader.read_from(buffer, at, n);
            const float* data = buffer.get_channel_buffer(0).get_data(n);
            out.insert(out.end(), data, data + n);
            at += n;
        }
        const std::vector<float> tpl = sweep(c.outputRate, 6000.0);
        const double offset = sweep_offset(out, double(c.outputRate), tpl, 100);

        // Whole, not only found: the correlation at its place against the
        // sweep's own energy (a sweep cut short by silence matches less)
        double match = 0.0, energy = 0.0;
        const size_t at = size_t(c.outputRate);
        for (size_t i = 0; i < tpl.size() && at + i < out.size(); ++i) {
            match += double(out[at + i]) * tpl[i];
            energy += double(tpl[i]) * tpl[i];
        }
        const double whole = match / energy;

        char what[200];
        std::snprintf(what, sizeof(what), "%u -> %u Hz, %s: the sweep 3 ms before the end in place and whole (%+.2f frames, %.3f)",
                      c.fileRate, c.outputRate, piece ? "in pieces" : "in one go", offset, whole);
        const bool ok = !std::isnan(offset) && std::fabs(offset) <= 0.05 && whole > 0.99;
        check(ok, what);
        if (ok) {
            std::printf("ok:   %s\n", what);
        }

        // A seek into the last frames, where more frames are to be dropped
        // after the common frame than the file has left: the read gives
        // what is left (the silence after the sweep) and ends. Not where
        // every output frame is a common one (96 -> 48 kHz).
        const uint common = std::gcd(c.fileRate, c.outputRate);
        const nframes_t period = c.outputRate / common;
        if (period < 2) {
            continue;
        }
        nframes_t last = (frames - 1) / period * period + period - 1;
        if (last >= frames) {
            last -= period;
        }
        const nframes_t left = frames - last;
        buffer.check_capacity(left, 1);
        buffer.silence_buffers();
        const nframes_t read = reader.read_from(buffer, last, left);
        const float* data = buffer.get_channel_buffer(0).get_data(left);
        const bool silent = std::all_of(data, data + left, [](float v) { return std::fabs(v) < 1e-3f; });
        std::snprintf(what, sizeof(what), "%u -> %u Hz, %s: a seek %u frames before the end, %u to drop, reads them (%u)",
                      c.fileRate, c.outputRate, piece ? "after pieces" : "after one go", left, period - 1, read);
        const bool lastOk = left < period - 1 && read == left && silent;
        check(lastOk, what);
        if (lastOk) {
            std::printf("ok:   %s\n", what);
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
