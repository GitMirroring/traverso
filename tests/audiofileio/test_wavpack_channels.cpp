// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of WavPack with any channel count: what WPAudioWriter writes
// has the channel mask of its channels (mono centre, stereo front left and
// right, more channels none), and WPAudioReader reads all channels back.

#include "AbstractAudioWriter.h"
#include "TAudioDecoder.h"
#include "TFileIOBuffer.h"

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
        {
            std::unique_ptr<AbstractAudioWriter> writer = AbstractAudioWriter::create_audio_writer(settings);
            if (!writer->open(path)) {
                std::printf("%u channels: cannot open the writer\n", c.channels);
                return 1;
            }
            std::vector<float> interleaved(size_t(Frames) * c.channels);
            for (nframes_t frame = 0; frame < Frames; ++frame) {
                for (uint channel = 0; channel < c.channels; ++channel) {
                    interleaved[size_t(frame) * c.channels + channel] = sample(frame, channel);
                }
            }
            writer->write(interleaved.data(), Frames);
            writer->close();
        }

        char what[96];
        char error[80];
        WavpackContext* wp = WavpackOpenFileInput(path.toUtf8().constData(), error, 0, 0);
        std::snprintf(what, sizeof(what), "%u channels: written with channel mask %d", c.channels, c.mask);
        check(wp && WavpackGetNumChannels(wp) == int(c.channels) && WavpackGetChannelMask(wp) == c.mask, what);
        if (wp) {
            WavpackCloseFile(wp);
        }

        std::unique_ptr<TAudioDecoder> decoder = open_audio_decoder(path);
        bool same = decoder && decoder->is_valid() && decoder->channel_count() == c.channels && decoder->frame_count() == Frames;
        TFileIOBuffer buffer;
        same = same && decoder->read(buffer, Frames) == Frames;
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

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all WavPack channel tests passed\n");
    return 0;
}
