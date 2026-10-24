// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of TFileIOBuffer: a buffer that served a stereo source (as
// the decode buffer of a disk thread does) also holds a source with more
// channels, whether the new read is smaller or larger than the ones before,
// and a 4-channel file read through it keeps its channels apart.

#include "TAudioReader.h"
#include "TFileIOBuffer.h"

#include <QFile>
#include <QTemporaryDir>

#include <cmath>
#include <cstdint>
#include <cstdio>

static int failures = 0;

static void check(bool ok, const char* what)
{
    std::printf("%s: %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) {
        ++failures;
    }
}

// The sample of a channel in the test files: one value per channel
static int16_t channel_value(uint channel)
{
    return int16_t((channel + 1) * 1000);
}

static void append_le(QByteArray& data, uint32_t value, int bytes)
{
    for (int i = 0; i < bytes; ++i) {
        data.append(char((value >> (8 * i)) & 0xff));
    }
}

// A 16-bit PCM WAV file with frames frames of channel_value() per channel
static bool write_wav(const QString& path, uint channels, uint frames)
{
    const uint rate = 44100;
    const uint blockAlign = channels * 2;
    const uint dataSize = frames * blockAlign;
    QByteArray data("RIFF");
    append_le(data, 36 + dataSize, 4);
    data.append("WAVEfmt ");
    append_le(data, 16, 4);
    append_le(data, 1, 2);
    append_le(data, channels, 2);
    append_le(data, rate, 4);
    append_le(data, rate * blockAlign, 4);
    append_le(data, blockAlign, 2);
    append_le(data, 16, 2);
    data.append("data");
    append_le(data, dataSize, 4);
    for (uint frame = 0; frame < frames; ++frame) {
        for (uint channel = 0; channel < channels; ++channel) {
            append_le(data, uint16_t(channel_value(channel)), 2);
        }
    }
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

// Reads count frames of a file into the buffer, as a stream does, and checks
// that every channel holds its own value
static bool read_file(TFileIOBuffer& buffer, const QString& path, uint channels, nframes_t count)
{
    const std::unique_ptr<TAudioReader> reader = TAudioReader::open(path);
    if (!reader || reader->channel_count() != channels) {
        return false;
    }
    if (reader->read_file_frames(buffer, 0, count) != count) {
        return false;
    }
    for (uint channel = 0; channel < channels; ++channel) {
        const float expected = float(channel_value(channel)) / 32768.0f;
        const float* samples = buffer.get_channel_buffer(channel).get_data(count);
        for (nframes_t frame = 0; frame < count; ++frame) {
            if (std::fabs(samples[frame] - expected) > 1.0e-6f) {
                return false;
            }
        }
    }
    return true;
}

int main()
{
    // More channels with a read smaller than the ones before
    {
        TFileIOBuffer buffer;
        buffer.check_capacity(4096, 2);
        buffer.check_capacity(1024, 4);
        bool roomy = buffer.get_file_io_interleaved_buffer().get_size() >= 1024 * 4;
        for (uint channel = 0; channel < 4; ++channel) {
            roomy = roomy && buffer.get_channel_buffer(channel).get_size() >= 1024;
        }
        check(roomy, "after stereo, a smaller read of 4 channels has room for each channel");
    }

    // More channels with a read larger than the ones before
    {
        TFileIOBuffer buffer;
        buffer.check_capacity(1024, 2);
        buffer.check_capacity(30720, 4);
        bool roomy = buffer.get_file_io_interleaved_buffer().get_size() >= 30720 * 4;
        for (uint channel = 0; channel < 4; ++channel) {
            roomy = roomy && buffer.get_channel_buffer(channel).get_size() >= 30720;
        }
        check(roomy, "after stereo, a larger read of 4 channels has room for each channel");
    }

    // The files, through one buffer, in both orders of size
    QTemporaryDir dir;
    const QString stereo = dir.filePath("stereo.wav");
    const QString quad = dir.filePath("quad.wav");
    if (!dir.isValid() || !write_wav(stereo, 2, 4096) || !write_wav(quad, 4, 4096)) {
        std::printf("cannot write the test files\n");
        return 1;
    }
    {
        TFileIOBuffer buffer;
        const bool stereoRead = read_file(buffer, stereo, 2, 4096);
        check(stereoRead && read_file(buffer, quad, 4, 1024), "a 4-channel file read after a larger stereo read");
    }
    {
        TFileIOBuffer buffer;
        const bool stereoRead = read_file(buffer, stereo, 2, 512);
        check(stereoRead && read_file(buffer, quad, 4, 4096), "a 4-channel file read after a smaller stereo read");
    }

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all TFileIOBuffer tests passed\n");
    return 0;
}
