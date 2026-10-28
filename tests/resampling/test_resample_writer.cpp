// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of TAudioWriter: sweeps written at another rate
// must all be in the file, where they belong and at their level, the last
// one too, which ends just before the end of the input. A writer that does
// not flush its resampler at the end loses what the resampler still holds.

#include "TAudioReader.h"
#include "TAudioEncodeSettings.h"
#include "TAudioBuffer.h"
#include "TAudioWriter.h"
#include "sweeps.h"

#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>
#include <tuple>

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

// The sweeps start at 0.5 s, 1 s apart; the input ends 3 ms after the last
static constexpr int SweepCount = 6;

static double sweep_start(int k)
{
    return 0.5 + k;
}

int main()
{
    QTemporaryDir dir;
    check(dir.isValid(), "a temporary directory");

    // Float, and PCM as an export or a recording describes it (24 bits:
    // three bytes a sample, dithered into 32 bit words); WAV through
    // sndfile, and WavPack. Not 8 bits in a WAV: sndfile has no signed 8 bit
    // WAV, and the writer does not open. Every case that converts the rate
    // with both resamplers, soxr and libsamplerate.
    const TraversoDAW::WriterType wav = TraversoDAW::WriterType::SNDFILE;
    const TraversoDAW::WriterType wavpack = TraversoDAW::WriterType::WAVPACK;
    struct Format {
        uint inputRate;
        uint fileRate;
        TraversoDAW::DataFormat format;
        TraversoDAW::WriterType writer;
    };
    struct Case {
        uint inputRate;
        uint fileRate;
        TraversoDAW::DataFormat format;
        TraversoDAW::WriterType writer;
        TResamplerChoice::Backend backend;
    };
    std::vector<Case> cases;
    for (const Format f : {Format{48000, 44100, TraversoDAW::DataFormat::FLOAT, wav}, Format{44100, 48000, TraversoDAW::DataFormat::FLOAT, wav},
                           Format{96000, 48000, TraversoDAW::DataFormat::FLOAT, wav}, Format{48000, 44100, TraversoDAW::DataFormat::PCM_24, wav},
                           Format{44100, 48000, TraversoDAW::DataFormat::PCM_24, wav}, Format{48000, 48000, TraversoDAW::DataFormat::PCM_24, wav},
                           Format{48000, 44100, TraversoDAW::DataFormat::FLOAT, wavpack}, Format{48000, 44100, TraversoDAW::DataFormat::PCM_24, wavpack},
                           Format{48000, 48000, TraversoDAW::DataFormat::PCM_24, wavpack}, Format{48000, 48000, TraversoDAW::DataFormat::PCM_16, wav},
                           Format{48000, 48000, TraversoDAW::DataFormat::PCM_16, wavpack}, Format{48000, 48000, TraversoDAW::DataFormat::PCM_S8, wavpack}}) {
        cases.push_back({f.inputRate, f.fileRate, f.format, f.writer, TResamplerChoice::Backend::LibSoxr});
        if (f.inputRate != f.fileRate) {
            cases.push_back({f.inputRate, f.fileRate, f.format, f.writer, TResamplerChoice::Backend::LibSampleRate});
        }
    }
    for (const Case& c : cases) {
        // The input: sweeps, the last one ending 3 ms before the end
        const std::vector<float> s = sweep(c.inputRate);
        const nframes_t inputFrames = nframes_t(std::lround((sweep_start(SweepCount - 1) + SweepSeconds + 0.003) * c.inputRate));
        std::vector<float> input(inputFrames, 0.0f);
        for (int k = 0; k < SweepCount; ++k) {
            std::copy(s.begin(), s.end(), input.begin() + long(std::lround(sweep_start(k) * c.inputRate)));
        }

        TAudioEncodeSettings settings;
        settings.writerType = c.writer;
        settings.fileFormat = c.writer == wavpack ? TraversoDAW::FileFormat::WAVPACK : TraversoDAW::FileFormat::WAV;
        settings.dataFormat = c.format;
        settings.channelCount = 1;
        settings.sampleRate = c.fileRate;
        settings.blockSize = 1024;
        const char* formatName = "float";
        switch (c.format) {
        case TraversoDAW::DataFormat::PCM_S8: formatName = "pcm8"; break;
        case TraversoDAW::DataFormat::PCM_16: formatName = "pcm16"; break;
        case TraversoDAW::DataFormat::PCM_24: formatName = "pcm24"; break;
        default: break;
        }
        settings.ditherType = GDitherNone;
        settings.resampler = {c.backend, qint16(TAudioResampler::default_quality(c.backend))};

        const QString backend = TAudioResampler::backend_name(c.backend);
        const QByteArray formatText = QString("%1 %2, %3").arg(c.writer == wavpack ? "wavpack" : "wav", formatName, backend).toLatin1();
        const char* format = formatText.constData();
        const QString path = dir.filePath(QString("written-%1-%2-%3-%4.%5").arg(c.inputRate).arg(c.fileRate).arg(formatName, backend)
                                              .arg(c.writer == wavpack ? "wv" : "wav"));
        {
            const std::unique_ptr<TAudioWriter> writer = TAudioWriter::open(settings, path, c.inputRate).value_or(nullptr);
            check(writer != nullptr, "opening the writer");
            if (!writer) {
                continue;
            }
            TAudioBuffer buffer;
            bool allTaken = true;
            for (nframes_t at = 0; at < inputFrames; at += settings.blockSize) {
                const nframes_t n = std::min<nframes_t>(settings.blockSize, inputFrames - at);
                buffer.check_capacity(n, 1);
                std::copy(input.begin() + at, input.begin() + at + n, buffer.get_channel_buffer(0).get_data(n));
                const nframes_t taken = writer->write(buffer, n).frames;
                allTaken = allTaken && taken == n;
            }
            const bool closed = writer->close().has_value();
            char what[200];
            std::snprintf(what, sizeof(what), "%u -> %u Hz, %s: every write takes all of its input, close() succeeds", c.inputRate, c.fileRate,
                          format);
            check(allTaken && closed, what);
        }

        // The file at its own rate
        const std::unique_ptr<TAudioReader> reader = TAudioReader::open(path).value_or(nullptr);
        check(reader != nullptr, "reading the file");
        if (!reader) {
            continue;
        }
        const nframes_t fileFrames = reader->file_frame_count();
        std::vector<float> file(fileFrames, 0.0f);
        TAudioBuffer buffer;
        if (fileFrames > 0) {
            std::ignore = reader->read_file_frames(buffer, 0, fileFrames);
        }
        std::copy_n(buffer.get_channel_buffer(0).get_data(fileFrames), fileFrames, file.begin());

        char what[200];
        const double expectedFrames = double(inputFrames) * c.fileRate / c.inputRate;
        std::snprintf(what, sizeof(what), "%u -> %u Hz, %s: the file holds all of the input (%u frames, %.1f expected)",
                      c.inputRate, c.fileRate, format, fileFrames, expectedFrames);
        check(std::fabs(double(fileFrames) - expectedFrames) <= 1.0, what);

        const std::vector<float> tpl = sweep(c.fileRate);
        for (int k = 0; k < SweepCount; ++k) {
            const double expected = sweep_start(k) * c.fileRate;
            const double offset = sweep_offset(file, expected, tpl, 2000);
            const double level = sweep_level(file, size_t(std::lround(expected)), tpl);
            std::snprintf(what, sizeof(what), "%u -> %u Hz, %s: sweep %d in place (%+.2f frames) at its level (%.4f)%s", c.inputRate,
                          c.fileRate, format, k, offset, level, k == SweepCount - 1 ? ", the last, 3 ms before the end" : "");
            check(!std::isnan(offset) && std::fabs(offset) <= 0.05 && std::fabs(level - 1.0) < 0.01, what);
        }
    }

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all checks passed\n");
    return 0;
}
