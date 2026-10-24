// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of SFAudioWriter's header: a WAV, W64 or AIFF file that is
// still being written (as after a kill or a crash, when nothing closes it)
// reads in full, with what was written so far, and the closed file too. A
// WAV header has the data size (libsndfile reads to the end without it,
// other programs do not).

#include "AbstractAudioWriter.h"
#include "TAudioDecoder.h"
#include "TFileIOBuffer.h"

#include <QFile>
#include <QTemporaryDir>

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
constexpr uint Channels = 2;
constexpr nframes_t Piece = Rate / 10;

static float sample(nframes_t frame, uint channel)
{
    return 0.25f * float(int((frame * 7 + channel * 13) % 2001) - 1000) / 1000.0f;
}

// The file holds frames frames of sample(), as a reader sees them
static bool reads(const QString& path, nframes_t frames)
{
    std::unique_ptr<TAudioDecoder> decoder = open_audio_decoder(path);
    if (!decoder || !decoder->is_valid() || decoder->frame_count() != frames || decoder->channel_count() != Channels) {
        return false;
    }
    TFileIOBuffer buffer;
    if (decoder->read(buffer, frames) != frames) {
        return false;
    }
    for (uint channel = 0; channel < Channels; ++channel) {
        const float* samples = buffer.get_channel_buffer(channel).get_data(frames);
        for (nframes_t frame = 0; frame < frames; ++frame) {
            if (std::fabs(samples[frame] - sample(frame, channel)) > 1.0e-6f) {
                return false;
            }
        }
    }
    return true;
}

// The size of a WAV file's data chunk, as its header says (what programs
// other than libsndfile go by), -1 without one
static qint64 wav_data_size(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return -1;
    }
    const QByteArray header = file.read(512);
    const qsizetype data = header.indexOf("data");
    if (data < 0 || data + 8 > header.size()) {
        return -1;
    }
    const auto byte = [&header](qsizetype i) { return quint32(quint8(header[i])); };
    return qint64(byte(data + 4) | byte(data + 5) << 8 | byte(data + 6) << 16 | byte(data + 7) << 24);
}

int main()
{
    QTemporaryDir dir;
    if (!dir.isValid()) {
        std::printf("no temporary directory\n");
        return 1;
    }
    const struct {
        const char* name;
        TraversoDAW::FileFormat format;
        const char* file;
    } formats[] = {{"WAV", TraversoDAW::FileFormat::WAV, "take.wav"},
                   {"W64", TraversoDAW::FileFormat::W64, "take.w64"},
                   {"AIFF", TraversoDAW::FileFormat::AIFF, "take.aiff"}};

    for (const auto& format : formats) {
        TAudioEncodeSettings settings;
        settings.writerType = TraversoDAW::WriterType::SNDFILE;
        settings.fileFormat = format.format;
        settings.dataFormat = TraversoDAW::DataFormat::FLOAT;
        settings.channelCount = Channels;
        settings.sampleRate = Rate;
        const QString path = dir.filePath(format.file);
        std::unique_ptr<AbstractAudioWriter> writer = AbstractAudioWriter::create_audio_writer(settings);
        if (!writer->open(path)) {
            std::printf("%s: cannot open the writer\n", format.name);
            return 1;
        }

        // A second in pieces, as a recording writes it
        std::vector<float> interleaved(size_t(Piece) * Channels);
        nframes_t frame = 0;
        for (int piece = 0; piece < 10; ++piece) {
            for (nframes_t i = 0; i < Piece; ++i) {
                for (uint channel = 0; channel < Channels; ++channel) {
                    interleaved[size_t(i) * Channels + channel] = sample(frame + i, channel);
                }
            }
            writer->write(interleaved.data(), Piece);
            frame += Piece;
        }

        char what[96];
        std::snprintf(what, sizeof(what), "%s: a file still being written reads what was written", format.name);
        check(reads(path, frame), what);
        if (format.format == TraversoDAW::FileFormat::WAV) {
            check(wav_data_size(path) == qint64(frame) * Channels * qint64(sizeof(float)), "WAV: the header of a file still being written has its data size");
        }
        writer->close();
        std::snprintf(what, sizeof(what), "%s: the closed file reads the same", format.name);
        check(reads(path, frame), what);
    }

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all SFAudioWriter header tests passed\n");
    return 0;
}
