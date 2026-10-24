// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of the audio decoders, through TAudioReader at the file's own
// rate (read_file_frames()): every decoder reads a file of every format it
// serves with the file's channels, rate and frames, and with the
// audio where it was written; a read after a seek gives what a read from the
// start gives at that place; a read that crosses the end of the file gives
// what is left and zeros after it, whatever the buffer held before.
//
// The files are made here: WAV, FLAC, Ogg Vorbis and MP3 with libsndfile,
// WavPack and AAC (ADTS) with Traverso's own writers, and when ffmpeg is
// installed AAC (ADTS and M4A) and an MP3 without gapless information with
// ffmpeg. Each channel holds a sweep of its own, so channels that are
// swapped or silent show, and audio that is shifted shows too (a sweep
// matches itself at one place only).

#include "TAudioReader.h"
#include "TAudioWriter.h"
#include "TFileIOBuffer.h"

#include <QProcess>
#include <QStandardPaths>
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

constexpr float Garbage = 0.7f;

// The audio of a test file: an odd length (no codec block ends there),
// channel 0 sweeping up from 200 Hz (exponentially) at 0.5, channel 1 down
// from 3 kHz to 300 Hz (linearly) at 0.3
struct Source
{
    uint rate{44100};
    uint channels{2};

    nframes_t frames() const { return rate + 1037; }

    float sample(nframes_t frame, uint channel) const
    {
        const double t = double(frame) / rate;
        const double length = double(frames()) / rate;
        if (channel == 0) {
            const double top = std::min(6000.0, 0.4 * rate);
            const double k = std::log(top / 200.0) / length;
            return float(0.5 * std::sin(2.0 * M_PI * 200.0 * (std::exp(k * t) - 1.0) / k));
        }
        return float(0.3 * std::sin(2.0 * M_PI * (3000.0 * t + (300.0 - 3000.0) * t * t / (2.0 * length))));
    }

    double level(uint channel) const { return (channel == 0 ? 0.5 : 0.3) / M_SQRT2; }

    std::vector<float> channel(uint channel) const
    {
        std::vector<float> samples(frames());
        for (nframes_t frame = 0; frame < frames(); ++frame) {
            samples[frame] = sample(frame, channel);
        }
        return samples;
    }

    std::vector<float> interleaved() const
    {
        std::vector<float> samples(size_t(frames()) * channels);
        for (nframes_t frame = 0; frame < frames(); ++frame) {
            for (uint c = 0; c < channels; ++c) {
                samples[size_t(frame) * channels + c] = sample(frame, c);
            }
        }
        return samples;
    }
};

static bool write_with_sndfile(const QString& path, const Source& source, int format)
{
    SF_INFO info{};
    info.samplerate = int(source.rate);
    info.channels = int(source.channels);
    info.format = format;
    SNDFILE* file = sf_open(path.toLocal8Bit().constData(), SFM_WRITE, &info);
    if (!file) {
        return false;
    }
    const std::vector<float> samples = source.interleaved();
    const sf_count_t written = sf_writef_float(file, samples.data(), source.frames());
    sf_close(file);
    return written == sf_count_t(source.frames());
}

static bool write_with_traverso(const QString& path, const Source& source, TraversoDAW::WriterType writer, TraversoDAW::FileFormat format)
{
    TAudioEncodeSettings settings;
    settings.writerType = writer;
    settings.fileFormat = format;
    // AAC: 16 bit (dithered there by TAudioWriter), as FaacAudioWriter takes it
    const bool aac = writer == TraversoDAW::WriterType::M4A;
    settings.dataFormat = aac ? TraversoDAW::DataFormat::PCM_16 : TraversoDAW::DataFormat::FLOAT;
    settings.channelCount = source.channels;
    settings.sampleRate = source.rate;
    const std::unique_ptr<TAudioWriter> file = TAudioWriter::open(settings, path, source.rate);
    if (!file) {
        return false;
    }
    TFileIOBuffer buffer;
    buffer.check_capacity(source.frames(), source.channels);
    for (uint channel = 0; channel < source.channels; ++channel) {
        const std::vector<float> samples = source.channel(channel);
        std::copy(samples.begin(), samples.end(), buffer.get_channel_buffer(channel).get_data(source.frames()));
    }
    const nframes_t written = file->write(buffer, source.frames());
    return file->close() && written == source.frames();
}

// With ffmpeg, from a 16 bit WAV of the source; false when ffmpeg is not
// installed
static bool write_with_ffmpeg(const QString& path, const Source& source, const QStringList& encode)
{
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    const QString wav = path + QStringLiteral(".source.wav");
    if (ffmpeg.isEmpty() || !write_with_sndfile(wav, source, SF_FORMAT_WAV | SF_FORMAT_PCM_16)) {
        return false;
    }
    QStringList arguments{QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-y"), QStringLiteral("-i"), wav};
    arguments << encode << path;
    return QProcess::execute(ffmpeg, arguments) == 0;
}

// How far b is shifted against a around their frames aFrom and bFrom: the lag
// within search frames either way at which length frames match best (b
// later than a: a positive lag)
static long best_lag(const std::vector<float>& a, size_t aFrom, const std::vector<float>& b, size_t bFrom, size_t length, long search)
{
    long bestLag = 0;
    double bestError = -1.0;
    for (long lag = -search; lag <= search; ++lag) {
        const long from = long(bFrom) + lag;
        if (from < 0 || size_t(from) + length > b.size() || aFrom + length > a.size()) {
            continue;
        }
        double error = 0.0;
        for (size_t i = 0; i < length; ++i) {
            const double d = double(b[size_t(from) + i]) - a[aFrom + i];
            error += d * d;
        }
        if (bestError < 0.0 || error < bestError) {
            bestError = error;
            bestLag = lag;
        }
    }
    return bestLag;
}

struct Format
{
    const char* name;
    const char* suffix;
    Source source;
    // How the file is made; false: the format cannot be written here
    std::function<bool(const QString&, const Source&)> write;
    // Lossless: the samples as written (within the format's resolution);
    // lossy: each channel's level, and the audio where it was written
    bool lossless;
    float resolution;
    // How far the frames may be off what was written: 0 where the format
    // says (MP3 with a LAME tag, Ogg, M4A), an AAC frame for ADTS, which does
    // not say where the audio starts and ends
    long frameTolerance = 0;
    // Whether the file says where the audio starts (an MP3 without a LAME
    // tag does not: the encoder's delay stays in)
    bool placement = true;
    // What is known wrong with this file (review of 2026-10-24): its failing
    // checks are printed as known, not counted
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

static void fill_with_garbage(TFileIOBuffer& buffer, nframes_t count, uint channels)
{
    buffer.check_capacity(count, channels);
    for (uint channel = 0; channel < channels; ++channel) {
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
    char what[200];
    const Source& source = format.source;
    const uint channels = source.channels;
    const QString path = dir.filePath(QStringLiteral("sweeps.%1").arg(QLatin1String(format.suffix)));
    if (!format.write(path, source)) {
        std::printf("%s: cannot be written here, skipped\n", format.name);
        return;
    }
    const std::unique_ptr<TAudioReader> reader = TAudioReader::open(path);
    std::snprintf(what, sizeof(what), "%s: opens (%s)", format.name, reader ? qPrintable(reader->decoder_type()) : "no decoder");
    check(reader != nullptr, what);
    if (!reader) {
        return;
    }
    TAudioReader* decoder = reader.get();

    const nframes_t frames = decoder->file_frame_count();
    const long frameError = long(frames) - long(source.frames());
    std::snprintf(what, sizeof(what), "%s: %u channels at %u Hz, %u frames (%+ld)", format.name, decoder->channel_count(), decoder->file_rate(),
                  frames, frameError);
    check_or_known(decoder->channel_count() == channels && decoder->file_rate() == source.rate && std::labs(frameError) <= format.frameTolerance,
                   what, format);
    if (decoder->channel_count() != channels || frames == 0) {
        return;
    }

    // From the start in reads of 1000 frames
    std::vector<std::vector<float>> sequential(channels);
    TFileIOBuffer buffer;
    nframes_t total = 0;
    for (nframes_t at = 0; at < frames; at += 1000) {
        const nframes_t count = std::min<nframes_t>(1000, frames - at);
        fill_with_garbage(buffer, count, channels);
        const nframes_t read = decoder->read_file_frames(buffer, at, count);
        total += read;
        for (uint channel = 0; channel < channels; ++channel) {
            const float* data = buffer.get_channel_buffer(channel).get_data(count);
            sequential[channel].insert(sequential[channel].end(), data, data + read);
        }
        if (read != count) {
            break;
        }
    }
    std::snprintf(what, sizeof(what), "%s: read from the start, %u of %u frames", format.name, total, frames);
    check_or_known(total == frames, what, format);
    if (total < 3 * 4096) {
        return;
    }

    if (format.lossless) {
        float worst = 0.0f;
        for (uint channel = 0; channel < channels; ++channel) {
            for (nframes_t frame = 0; frame < total; ++frame) {
                worst = std::max(worst, std::fabs(sequential[channel][frame] - source.sample(frame, channel)));
            }
        }
        std::snprintf(what, sizeof(what), "%s: the samples as written (worst %.2g)", format.name, double(worst));
        check(worst <= format.resolution, what);
    } else {
        // The level of each sweep in the middle of the file (codec delay and
        // edges left out)
        for (uint channel = 0; channel < channels; ++channel) {
            const double level = rms(sequential[channel], total / 4, total * 3 / 4);
            std::snprintf(what, sizeof(what), "%s: channel %u its own sweep (level %.3f of %.3f)", format.name, channel, level, source.level(channel));
            check(std::fabs(level - source.level(channel)) < 0.05, what);
        }

        // Where it was written: the sweep of each channel matches itself at
        // the same frame (an encoder's delay is the decoder's to take out)
        for (uint channel = 0; format.placement && channel < channels; ++channel) {
            const size_t from = total / 3;
            const long lag = best_lag(source.channel(channel), from, sequential[channel], from, 4096, 3000);
            std::snprintf(what, sizeof(what), "%s: channel %u where it was written (%+ld frames)", format.name, channel, lag);
            check_or_known(lag == 0, what, format);
        }
    }

    // A read after a seek gives what the read from the start gave there:
    // forwards, backwards, near the start, and into the last frames
    const nframes_t seekCount = 2000;
    const float tolerance = format.lossless ? 0.0f : 0.02f;
    for (const nframes_t first : {total * 2 / 3, total / 8, nframes_t(1), total / 4 + 345, total - 1500}) {
        fill_with_garbage(buffer, seekCount, channels);
        const nframes_t read = decoder->read_file_frames(buffer, first, seekCount);
        const nframes_t expected = std::min(seekCount, total - first);
        float worst = 0.0f;
        for (uint channel = 0; channel < channels; ++channel) {
            const float* data = buffer.get_channel_buffer(channel).get_data(seekCount);
            for (nframes_t i = 0; i < std::min(read, expected); ++i) {
                worst = std::max(worst, std::fabs(data[i] - sequential[channel][first + i]));
            }
        }
        // How far off a seek that missed landed
        long lag = 0;
        if (worst > tolerance && read > 1000) {
            const float* data = buffer.get_channel_buffer(0).get_data(seekCount);
            lag = best_lag(sequential[0], first, std::vector<float>(data, data + read), 0, 512, 3000);
        }
        std::snprintf(what, sizeof(what), "%s: a seek to frame %u reads what was there (%u of %u frames, worst %.2g, %+ld frames)", format.name,
                      first, read, expected, double(worst), lag);
        check_or_known(read == expected && worst <= tolerance, what, format);
    }

    // Across the end of the file: what is left, zeros after it
    {
        const nframes_t count = 500;
        const nframes_t first = total - 100;
        fill_with_garbage(buffer, count, channels);
        const nframes_t read = decoder->read_file_frames(buffer, first, count);
        bool zeros = true;
        for (uint channel = 0; channel < channels; ++channel) {
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
        fill_with_garbage(buffer, count, channels);
        const nframes_t read = decoder->read_file_frames(buffer, seekPastEnd ? frames + 10 : frames, count);
        bool zeros = true;
        for (uint channel = 0; channel < channels; ++channel) {
            const float* data = buffer.get_channel_buffer(channel).get_data(count);
            for (nframes_t i = 0; i < count; ++i) {
                zeros = zeros && data[i] == 0.0f;
            }
        }
        std::snprintf(what, sizeof(what), "%s: a read %s gives no frames, only zeros (%u)", format.name,
                      seekPastEnd ? "of a frame past the end" : "at the end", read);
        check_or_known(read == 0 && zeros, what, format);
    }
}

int main()
{
    QTemporaryDir dir;
    if (!dir.isValid()) {
        std::printf("no temporary directory\n");
        return 1;
    }

    const auto sndfile = [](int format) {
        return [format](const QString& path, const Source& source) { return write_with_sndfile(path, source, format); };
    };
    const auto ffmpeg = [](QStringList encode) {
        return [encode](const QString& path, const Source& source) { return write_with_ffmpeg(path, source, encode); };
    };
    const Source stereo;
    const Source mono22{22050, 1};
    const std::vector<Format> formats = {
        {"WAV float (sndfile)", "wav", stereo, sndfile(SF_FORMAT_WAV | SF_FORMAT_FLOAT), true, 0.0f},
        {"WAV 16 bit (sndfile)", "16.wav", stereo, sndfile(SF_FORMAT_WAV | SF_FORMAT_PCM_16), true, 1.0f / 32768},
        {"FLAC 24 bit (sndfile)", "flac", stereo, sndfile(SF_FORMAT_FLAC | SF_FORMAT_PCM_24), true, 1.0f / 8388608},
        {"Ogg Vorbis (sndfile)", "ogg", stereo, sndfile(SF_FORMAT_OGG | SF_FORMAT_VORBIS), false, 0.0f},
        {"MP3 with a LAME tag", "mp3", stereo, sndfile(SF_FORMAT_MPEG | SF_FORMAT_MPEG_LAYER_III), false, 0.0f},
        {"MP3 MPEG-2 mono 22.05 kHz", "22.mp3", mono22, sndfile(SF_FORMAT_MPEG | SF_FORMAT_MPEG_LAYER_III), false, 0.0f},
        {"MP3 without gapless information (ffmpeg)", "nolame.mp3", stereo,
         ffmpeg({QStringLiteral("-c:a"), QStringLiteral("libmp3lame"), QStringLiteral("-b:a"), QStringLiteral("128k"), QStringLiteral("-write_xing"),
                 QStringLiteral("0")}),
         false, 0.0f, 3000, false},
        {"WavPack float", "wv", stereo,
         [](const QString& path, const Source& source) {
             return write_with_traverso(path, source, TraversoDAW::WriterType::WAVPACK, TraversoDAW::FileFormat::WAVPACK);
         },
         true, 0.0f},
#if defined(M4A_DECODE_SUPPORT)
        {"AAC in ADTS (ffmpeg)", "aac", stereo,
         ffmpeg({QStringLiteral("-c:a"), QStringLiteral("aac"), QStringLiteral("-b:a"), QStringLiteral("128k"), QStringLiteral("-f"),
                 QStringLiteral("adts")}),
         false, 0.0f, 1024},
        // ffmpeg's edit list says 23 frames fewer than it was given (ffprobe
        // too): the decoder takes the file's word
        {"AAC in M4A (ffmpeg)", "m4a", stereo, ffmpeg({QStringLiteral("-c:a"), QStringLiteral("aac"), QStringLiteral("-b:a"), QStringLiteral("128k")}),
         false, 0.0f, 64},
#endif
#if defined(M4A_ENCODE_SUPPORT) && defined(M4A_DECODE_SUPPORT)
        {"AAC in ADTS (Traverso's writer)", "traverso.aac", stereo,
         [](const QString& path, const Source& source) {
             return write_with_traverso(path, source, TraversoDAW::WriterType::M4A, TraversoDAW::FileFormat::UNKNOWN);
         },
         false, 0.0f, 1024, true, "libfaac leaves out the first 2048 frames"},
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
