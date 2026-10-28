// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of WavPack with any channel count: what WPAudioWriter writes
// has the channel mask of its channels (mono centre, stereo front left and
// right, more channels none), and WPAudioReader reads all channels back.

#include "TAudioReader.h"
#include "TAudioWriter.h"
#include "TAudioBuffer.h"

#include <QTemporaryDir>

#include <wavpack/wavpack.h>

#include <cmath>
#include <cstdio>
#include <vector>

static int failures = 0;

static void check(bool ok, const char* what)
{
    std::printf("%s: %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) {
        ++failures;
    }
}

constexpr uint Rate = 44100;
constexpr nframes_t Frames = Rate;

static float sample(nframes_t frame, uint channel)
{
    return 0.1f * float(channel + 1) + 0.05f * std::sin(float(frame) * 0.01f * float(channel + 1));
}

// Frames frames of sample() in channels channels into a file, as Traverso
// writes one
static bool write_file(const QString& path, const TAudioEncodeSettings& settings)
{
    const std::unique_ptr<TAudioWriter> writer = TAudioWriter::open(settings, path, settings.sampleRate).value_or(nullptr);
    if (!writer) {
        return false;
    }
    TAudioBuffer buffer;
    buffer.check_capacity(Frames, settings.channelCount);
    for (uint channel = 0; channel < settings.channelCount; ++channel) {
        float* samples = buffer.get_channel_buffer(channel).get_data(Frames);
        for (nframes_t frame = 0; frame < Frames; ++frame) {
            samples[frame] = sample(frame, channel);
        }
    }
    const bool written = writer->write(buffer, Frames).frames == Frames;
    return writer->close() && written;
}

int main()
{
    QTemporaryDir dir;
    if (!dir.isValid()) {
        std::printf("no temporary directory\n");
        return 1;
    }

    const struct {
        uint channels;
        int mask;
    } cases[] = {{1, 4}, {2, 3}, {4, 0}, {6, 0}};

    for (const auto& c : cases) {
        TAudioEncodeSettings settings;
        settings.writerType = TraversoDAW::WriterType::WAVPACK;
        settings.fileFormat = TraversoDAW::FileFormat::WAVPACK;
        settings.dataFormat = TraversoDAW::DataFormat::FLOAT;
        settings.channelCount = c.channels;
        settings.sampleRate = Rate;
        const QString path = dir.filePath(QStringLiteral("take-%1.wv").arg(c.channels));
        if (!write_file(path, settings)) {
            std::printf("%u channels: cannot write the file\n", c.channels);
            return 1;
        }

        char what[96];
        char error[80];
        WavpackContext* wp = WavpackOpenFileInput(path.toUtf8().constData(), error, 0, 0);
        std::snprintf(what, sizeof(what), "%u channels: written with channel mask %d", c.channels, c.mask);
        check(wp && WavpackGetNumChannels(wp) == int(c.channels) && WavpackGetChannelMask(wp) == c.mask, what);
        if (wp) {
            WavpackCloseFile(wp);
        }

        const std::unique_ptr<TAudioReader> reader = TAudioReader::open(path).value_or(nullptr);
        bool same = reader && reader->channel_count() == c.channels && reader->file_frame_count() == Frames;
        TAudioBuffer buffer;
        same = same && reader->read_file_frames(buffer, 0, Frames).frames == Frames;
        for (uint channel = 0; same && channel < c.channels; ++channel) {
            const float* samples = buffer.get_channel_buffer(channel).get_data(Frames);
            for (nframes_t frame = 0; frame < Frames; ++frame) {
                if (std::fabs(samples[frame] - sample(frame, channel)) > 1.0e-6f) {
                    same = false;
                    break;
                }
            }
        }
        std::snprintf(what, sizeof(what), "%u channels: read back, every channel", c.channels);
        check(same, what);
    }

    // The compression asked for is the file's (the settings' choice reaches
    // the encoder): WavPack says in its mode flags how a file was packed
    using Compression = TAudioEncodeSettings::WavPackCompression;
    const struct {
        Compression compression;
        int mode;
        const char* name;
    } compressions[] = {{Compression::Fast, MODE_FAST, "fast"}, {Compression::High, MODE_HIGH, "high"},
                        {Compression::VeryHigh, MODE_VERY_HIGH, "very high"}};
    for (const auto& c : compressions) {
        TAudioEncodeSettings settings;
        settings.writerType = TraversoDAW::WriterType::WAVPACK;
        settings.fileFormat = TraversoDAW::FileFormat::WAVPACK;
        settings.dataFormat = TraversoDAW::DataFormat::FLOAT;
        settings.channelCount = 2;
        settings.sampleRate = Rate;
        settings.wavpackCompression = c.compression;
        const QString path = dir.filePath(QStringLiteral("compression-%1.wv").arg(int(c.compression)));
        const bool ok = write_file(path, settings);
        char error[80];
        WavpackContext* wp = WavpackOpenFileInput(path.toUtf8().constData(), error, 0, 0);
        const int mode = wp ? WavpackGetMode(wp) : 0;
        if (wp) {
            WavpackCloseFile(wp);
        }
        const int compressionModes = MODE_FAST | MODE_HIGH | MODE_VERY_HIGH;
        const int expected = c.mode == MODE_VERY_HIGH ? (MODE_HIGH | MODE_VERY_HIGH) : c.mode;
        char what[96];
        std::snprintf(what, sizeof(what), "compression %s: written so (mode 0x%x)", c.name, mode & compressionModes);
        check(ok && (mode & compressionModes) == expected, what);
    }

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all WavPack channel tests passed\n");
    return 0;
}
