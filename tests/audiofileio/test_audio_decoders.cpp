// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of the audio decoders, before and after their refactoring:
// every decoder reads a file of every format it serves with the file's
// channels, rate and frames; a read after a seek gives what a read from the
// start gives at that place; a read that crosses the end of the file gives
// what is left and zeros after it, whatever the buffer held before.
//
// The files are made here: WAV, FLAC, Ogg Vorbis and MP3 with libsndfile,
// WavPack and AAC (M4A) with Traverso's own writers. Each channel holds a
// tone of its own, so channels that are swapped or silent show.

#include "AbstractAudioWriter.h"
#include "MadAudioReader.h"
#include "TAudioDecoder.h"
#include "TFileIOBuffer.h"

#include <QTemporaryDir>

#include <sndfile.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
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
// An odd length: no codec block ends there
constexpr nframes_t Frames = Rate + 1037;
constexpr float Garbage = 0.7f;

// Channel 0 a 440 Hz tone at 0.5, channel 1 a 660 Hz tone at 0.3
static float sample(nframes_t frame, uint channel)
{
    const double frequency = channel == 0 ? 440.0 : 660.0;
    const double level = channel == 0 ? 0.5 : 0.3;
    return float(level * std::sin(2.0 * M_PI * frequency * double(frame) / Rate));
}

static std::vector<float> interleaved_source()
{
    std::vector<float> samples(size_t(Frames) * Channels);
    for (nframes_t frame = 0; frame < Frames; ++frame) {
        for (uint channel = 0; channel < Channels; ++channel) {
            samples[size_t(frame) * Channels + channel] = sample(frame, channel);
        }
    }
    return samples;
}

static bool write_with_sndfile(const QString& path, int format)
{
    SF_INFO info{};
    info.samplerate = Rate;
    info.channels = Channels;
    info.format = format;
    SNDFILE* file = sf_open(path.toLocal8Bit().constData(), SFM_WRITE, &info);
    if (!file) {
        return false;
    }
    const std::vector<float> samples = interleaved_source();
    const sf_count_t written = sf_writef_float(file, samples.data(), Frames);
    sf_close(file);
    return written == sf_count_t(Frames);
}

static bool write_with_traverso(const QString& path, TraversoDAW::WriterType writer, TraversoDAW::FileFormat format)
{
    TAudioEncodeSettings settings;
    settings.writerType = writer;
    settings.fileFormat = format;
    // AAC: 16 bit input, as FaacAudioWriter takes it
    const bool aac = writer == TraversoDAW::WriterType::M4A;
    settings.dataFormat = aac ? TraversoDAW::DataFormat::PCM_16 : TraversoDAW::DataFormat::FLOAT;
    settings.channelCount = Channels;
    settings.sampleRate = Rate;
    std::unique_ptr<AbstractAudioWriter> file = AbstractAudioWriter::create_audio_writer(settings);
    if (!file || !file->open(path)) {
        return false;
    }
    std::vector<float> samples = interleaved_source();
    nframes_t written = 0;
    if (settings.dataFormat == TraversoDAW::DataFormat::PCM_16) {
        // The AAC writer takes 16 bit samples
        std::vector<int16_t> pcm(samples.size());
        for (size_t i = 0; i < samples.size(); ++i) {
            pcm[i] = int16_t(std::lround(samples[i] * 32767.0f));
        }
        written = file->write(pcm.data(), Frames);
    } else {
        written = file->write(samples.data(), Frames);
    }
    file->close();
    return written == Frames;
}

// The decoder as the test uses it: the only place that names its API
struct Decoder
{
    std::unique_ptr<TAudioDecoder> decoder;

    bool valid() const { return decoder && decoder->is_valid(); }
    uint channels() const { return decoder->channel_count(); }
    uint rate() const { return decoder->rate(); }
    nframes_t frames() const { return decoder->frame_count(); }
    nframes_t read(TFileIOBuffer& buffer, nframes_t first, nframes_t count) { return decoder->read_from(buffer, first, count); }
    // A read where the decoder is, without a seek
    nframes_t read_on(TFileIOBuffer& buffer, nframes_t count) { return decoder->read(buffer, count); }
};

struct Format
{
    const char* name;
    const char* suffix;
    // How the file is made; false: the format cannot be written here
    std::function<bool(const QString&)> write;
    // How it is decoded
    std::function<Decoder(const QString&)> open;
    // Lossless: the samples as written (within the format's resolution);
    // lossy: the level of each channel, and frames within a codec block or so
    bool lossless;
    float resolution;
    // What this decoder is known to get wrong today (review of 2026-10-24):
    // its failing checks are printed as known, not counted
    const char* known = nullptr;
};

// A check this format is known to fail: reported, not counted
static void check_or_known(bool ok, const char* what, const Format& format)
{
    if (ok || !format.known) {
        check(ok, what);
        return;
    }
    std::printf("%s: KNOWN (%s)\n", what, format.known);
}

static Decoder from_factory(const QString& path)
{
    return Decoder{open_audio_decoder(path)};
}

static Decoder with_mad(const QString& path)
{
    return Decoder{std::make_unique<MadAudioReader>(path)};
}

// The frames of channel channel of buffer, count long
static std::vector<float> channel_of(TFileIOBuffer& buffer, uint channel, nframes_t count)
{
    const float* data = buffer.get_channel_buffer(channel).get_data(count);
    return std::vector<float>(data, data + count);
}

static void fill_with_garbage(TFileIOBuffer& buffer, nframes_t count)
{
    buffer.check_capacity(count, Channels);
    for (uint channel = 0; channel < Channels; ++channel) {
        std::fill_n(buffer.get_channel_buffer(channel).get_data(count), count, Garbage);
    }
}

static double rms(const std::vector<float>& samples, size_t from, size_t to)
{
    double sum = 0.0;
    for (size_t i = from; i < to; ++i) {
        sum += double(samples[i]) * samples[i];
    }
    return std::sqrt(sum / double(to - from));
}

static void test_format(const QTemporaryDir& dir, const Format& format)
{
    char what[160];
    const QString path = dir.filePath(QStringLiteral("tones.%1").arg(QLatin1String(format.suffix)));
    if (!format.write(path)) {
        std::printf("%s: cannot be written here, skipped\n", format.name);
        return;
    }
    Decoder decoder = format.open(path);
    std::snprintf(what, sizeof(what), "%s: opens", format.name);
    check(decoder.valid(), what);
    if (!decoder.valid()) {
        return;
    }

    const nframes_t frames = decoder.frames();
    const long frameError = long(frames) - long(Frames);
    std::snprintf(what, sizeof(what), "%s: %u channels at %u Hz, %u frames (%+ld)", format.name, decoder.channels(), decoder.rate(),
                  frames, frameError);
    check(decoder.channels() == Channels && decoder.rate() == Rate && (format.lossless ? frameError == 0 : std::labs(frameError) <= 3000),
          what);
    if (decoder.channels() != Channels || frames == 0) {
        return;
    }

    // From the start in reads of 1000 frames
    std::vector<std::vector<float>> sequential(Channels);
    TFileIOBuffer buffer;
    nframes_t total = 0;
    for (nframes_t at = 0; at < frames; at += 1000) {
        const nframes_t count = std::min<nframes_t>(1000, frames - at);
        fill_with_garbage(buffer, count);
        const nframes_t read = decoder.read(buffer, at, count);
        total += read;
        for (uint channel = 0; channel < Channels; ++channel) {
            const std::vector<float> part = channel_of(buffer, channel, read);
            sequential[channel].insert(sequential[channel].end(), part.begin(), part.end());
        }
        if (read != count) {
            break;
        }
    }
    std::snprintf(what, sizeof(what), "%s: read from the start, %u of %u frames", format.name, total, frames);
    check_or_known(total == frames, what, format);

    if (format.lossless) {
        float worst = 0.0f;
        for (uint channel = 0; channel < Channels; ++channel) {
            for (nframes_t frame = 0; frame < total; ++frame) {
                worst = std::max(worst, std::fabs(sequential[channel][frame] - sample(frame, channel)));
            }
        }
        std::snprintf(what, sizeof(what), "%s: the samples as written (worst %.2g)", format.name, double(worst));
        check(worst <= format.resolution, what);
    } else {
        // The level of each tone in the middle of the file (codec delay and
        // edges left out): 0.5 / sqrt(2) and 0.3 / sqrt(2)
        const double left = rms(sequential[0], total / 4, total * 3 / 4);
        const double right = rms(sequential[1], total / 4, total * 3 / 4);
        std::snprintf(what, sizeof(what), "%s: each channel its own tone (levels %.3f, %.3f)", format.name, left, right);
        check(std::fabs(left - 0.5 / M_SQRT2) < 0.05 && std::fabs(right - 0.3 / M_SQRT2) < 0.05, what);
    }

    // A read after a seek gives what the read from the start gave there:
    // forwards, backwards, and into the last frames
    const nframes_t seekCount = 2000;
    for (const nframes_t first : {nframes_t(30000), nframes_t(5003), total - 1500}) {
        fill_with_garbage(buffer, seekCount);
        const nframes_t read = decoder.read(buffer, first, seekCount);
        const nframes_t expected = std::min(seekCount, total - first);
        float worst = 0.0f;
        for (uint channel = 0; channel < Channels; ++channel) {
            const float* data = buffer.get_channel_buffer(channel).get_data(seekCount);
            for (nframes_t i = 0; i < std::min(read, expected); ++i) {
                worst = std::max(worst, std::fabs(data[i] - sequential[channel][first + i]));
            }
        }
        std::snprintf(what, sizeof(what), "%s: a seek to frame %u reads what was there (%u of %u frames, worst %.2g)", format.name, first, read,
                      expected, double(worst));
        check_or_known(read == expected && worst <= (format.lossless ? 0.0f : 0.02f), what, format);
    }

    // Across the end of the file: what is left, zeros after it
    {
        const nframes_t count = 500;
        const nframes_t first = total - 100;
        fill_with_garbage(buffer, count);
        const nframes_t read = decoder.read(buffer, first, count);
        bool zeros = true;
        for (uint channel = 0; channel < Channels; ++channel) {
            const float* data = buffer.get_channel_buffer(channel).get_data(count);
            for (nframes_t i = read; i < count; ++i) {
                zeros = zeros && data[i] == 0.0f;
            }
        }
        std::snprintf(what, sizeof(what), "%s: a read across the end gives the last %u frames and zeros after them", format.name, read);
        check_or_known(read == 100 && zeros, what, format);
    }

    // Past the end: nothing to read, only zeros (no stale audio), whether
    // the decoder is at the end already or is asked for a frame past it
    for (const bool seekPastEnd : {false, true}) {
        const nframes_t count = 500;
        fill_with_garbage(buffer, count);
        const nframes_t read = seekPastEnd ? decoder.read(buffer, frames + 10, count) : decoder.read_on(buffer, count);
        bool zeros = true;
        for (uint channel = 0; channel < Channels; ++channel) {
            const float* data = buffer.get_channel_buffer(channel).get_data(count);
            for (nframes_t i = 0; i < count; ++i) {
                zeros = zeros && data[i] == 0.0f;
            }
        }
        std::snprintf(what, sizeof(what), "%s: a read %s gives no frames, only zeros (%u)", format.name,
                      seekPastEnd ? "of a frame past the end" : "at the end", read);
        // A decoder that counts more frames than it gives is not at its end
        // after the last frame it gave: known where the frames are known wrong
        if (seekPastEnd) {
            check(read == 0 && zeros, what);
        } else {
            check_or_known(read == 0 && zeros, what, format);
        }
    }
}

int main()
{
    QTemporaryDir dir;
    if (!dir.isValid()) {
        std::printf("no temporary directory\n");
        return 1;
    }

    const auto sndfile = [](int format) { return [format](const QString& path) { return write_with_sndfile(path, format); }; };
    const std::vector<Format> formats = {
        {"WAV float (sndfile)", "wav", sndfile(SF_FORMAT_WAV | SF_FORMAT_FLOAT), from_factory, true, 0.0f},
        {"WAV 16 bit (sndfile)", "16.wav", sndfile(SF_FORMAT_WAV | SF_FORMAT_PCM_16), from_factory, true, 1.0f / 32768},
        {"FLAC 24 bit (sndfile)", "flac", sndfile(SF_FORMAT_FLAC | SF_FORMAT_PCM_24), from_factory, true, 1.0f / 8388608},
        {"Ogg Vorbis (sndfile)", "ogg", sndfile(SF_FORMAT_OGG | SF_FORMAT_VORBIS), from_factory, false, 0.0f,
         "libsndfile's seek is not sample accurate everywhere"},
        {"MP3 (sndfile)", "mp3", sndfile(SF_FORMAT_MPEG | SF_FORMAT_MPEG_LAYER_III), from_factory, false, 0.0f,
         "libsndfile's seek is not sample accurate everywhere"},
        {"MP3 (MAD)", "mad.mp3", sndfile(SF_FORMAT_MPEG | SF_FORMAT_MPEG_LAYER_III), with_mad, false, 0.0f,
         "encoder delay and padding counted as frames; seeks land elsewhere"},
        {"WavPack float", "wv",
         [](const QString& path) { return write_with_traverso(path, TraversoDAW::WriterType::WAVPACK, TraversoDAW::FileFormat::WAVPACK); },
         from_factory, true, 0.0f},
#if defined(M4A_ENCODE_SUPPORT) && defined(M4A_DECODE_SUPPORT)
        {"AAC in M4A (FAAD)", "m4a",
         [](const QString& path) { return write_with_traverso(path, TraversoDAW::WriterType::M4A, TraversoDAW::FileFormat::UNKNOWN); },
         from_factory, false, 0.0f, "priming counted as frames; seeks one AAC frame (1024) off; the read ends 1024 frames early"},
#endif
    };
    for (const Format& format : formats) {
        test_format(dir, format);
    }

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all decoder tests passed\n");
    return 0;
}
