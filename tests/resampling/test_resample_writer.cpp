// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of TResampleAudioWriter: sweeps written at another rate
// must all be in the file, where they belong, the last one too, which ends
// just before the end of the input. A writer that does not flush its
// resampler at the end loses what the resampler still holds.

#include "ResampleAudioReader.h"
#include "TAudioEncodeSettings.h"
#include "TFileIOBuffer.h"
#include "TResampleAudioWriter.h"

#include <QTemporaryDir>

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

static constexpr double SweepSeconds = 0.020;
static constexpr int SweepCount = 6;

static std::vector<float> sweep(uint rate)
{
    const int n = int(std::lround(SweepSeconds * rate));
    std::vector<float> s(size_t(n), 0.0f);
    const double k = (6000.0 - 500.0) / SweepSeconds;
    for (int i = 0; i < n; ++i) {
        const double t = double(i) / rate;
        const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / (n - 1));
        s[size_t(i)] = float(0.5 * w * std::sin(2.0 * M_PI * (500.0 * t + 0.5 * k * t * t)));
    }
    return s;
}

// The sweeps start at 0.5 s, 1 s apart; the input ends 3 ms after the last
static double sweep_start(int k)
{
    return 0.5 + k;
}

static double sweep_offset(const std::vector<float>& out, double expected, const std::vector<float>& tpl, int search)
{
    // The window stops at the end of the file: the last sweep is close to it
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

int main()
{
    QTemporaryDir dir;
    check(dir.isValid(), "a temporary directory");

    struct Case { uint inputRate; uint fileRate; };
    for (const Case c : {Case{48000, 44100}, Case{44100, 48000}, Case{96000, 48000}}) {
        // The input: sweeps, the last one ending 3 ms before the end
        const std::vector<float> s = sweep(c.inputRate);
        const nframes_t inputFrames = nframes_t(std::lround((sweep_start(SweepCount - 1) + SweepSeconds + 0.003) * c.inputRate));
        std::vector<float> input(inputFrames, 0.0f);
        for (int k = 0; k < SweepCount; ++k) {
            std::copy(s.begin(), s.end(), input.begin() + long(std::lround(sweep_start(k) * c.inputRate)));
        }

        TAudioEncodeSettings settings;
        settings.writerType = TraversoDAW::WriterType::SNDFILE;
        settings.fileFormat = TraversoDAW::FileFormat::WAV;
        settings.dataFormat = TraversoDAW::DataFormat::FLOAT;
        settings.channelCount = 1;
        settings.sampleRate = c.fileRate;
        settings.blockSize = 1024;
        settings.sampleBytes = 4;
        settings.bitDepth = 32;
        settings.sampleRateConversionQuality = 2;

        const QString path = dir.filePath(QString("written-%1-%2.wav").arg(c.inputRate).arg(c.fileRate));
        {
            TResampleAudioWriter writer(settings);
            check(writer.open(path, c.inputRate), "opening the writer");
            TFileIOBuffer buffer;
            for (nframes_t at = 0; at < inputFrames; at += settings.blockSize) {
                const nframes_t n = std::min<nframes_t>(settings.blockSize, inputFrames - at);
                buffer.check_capacity(n, 1);
                std::copy(input.begin() + at, input.begin() + at + n, buffer.get_channel_buffer(0).get_data(n));
                writer.write_planar(buffer, n);
            }
            writer.close();
        }

        // The file at its own rate
        TResampleAudioReader reader(path);
        const nframes_t fileFrames = reader.get_nframes();
        std::vector<float> file(fileFrames, 0.0f);
        TFileIOBuffer buffer;
        buffer.check_capacity(fileFrames, 1);
        reader.read_from(buffer, nframes_t(0), fileFrames);
        std::copy_n(buffer.get_channel_buffer(0).get_data(fileFrames), fileFrames, file.begin());

        char what[200];
        const double expectedFrames = double(inputFrames) * c.fileRate / c.inputRate;
        std::snprintf(what, sizeof(what), "%u -> %u Hz: the file holds all of the input (%u frames, %.1f expected)",
                      c.inputRate, c.fileRate, fileFrames, expectedFrames);
        check(std::fabs(double(fileFrames) - expectedFrames) <= 1.0, what);

        const std::vector<float> tpl = sweep(c.fileRate);
        for (int k = 0; k < SweepCount; ++k) {
            const double offset = sweep_offset(file, sweep_start(k) * c.fileRate, tpl, 2000);
            std::snprintf(what, sizeof(what), "%u -> %u Hz: sweep %d in place (%+.2f frames)%s", c.inputRate, c.fileRate, k,
                          offset, k == SweepCount - 1 ? ", the last, 3 ms before the end" : "");
            check(!std::isnan(offset) && std::fabs(offset) <= 0.05, what);
        }
    }

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all checks passed\n");
    return 0;
}
