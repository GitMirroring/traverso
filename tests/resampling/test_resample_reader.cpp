// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of TAudioReader: a file of short sweeps, read at
// another rate the way the stream reader reads it, must come out with every
// sweep where it belongs on the file's timeline, within a twentieth of a frame. A
// reader that pads a short read of the resampler with silence fails it: the
// silence shifts all that follows. And the reads of the stream reader after
// a seek before a clip, at any fraction of a frame, are one read, bit for
// bit: no frame lost or doubled where they join.

#include "TAudioReader.h"
#include "TFileIOBuffer.h"
#include "TTimeRef.h"
#include "sweeps.h"

#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <utility>
#include <vector>
#include <tuple>

static int failures = 0;

static void check(bool ok, const char* what)
{
    if (!ok) {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}

// One sweep per second, from 1 s on
static constexpr int SweepCount = 12;

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
static std::vector<float> read_all(const QString& path, uint outputRate, TAudioResampler::BackendType backend, int quality,
                                   Pattern pattern, nframes_t& first, nframes_t fileFrames, uint fileRate)
{
    const std::unique_ptr<TAudioReader> opened = TAudioReader::open(path).value_or(nullptr);
    check(opened != nullptr, "the file opens");
    if (!opened) {
        return {};
    }
    TAudioReader& reader = *opened;
    std::ignore = reader.set_output_rate_and_resampler(outputRate, backend, quality);

    // The length and the frames are the file's, at any output rate; the
    // frames at the output rate end where the file ends
    check(reader.length() == TTimeRef(fileFrames, fileRate), "the length is the file's");
    check(reader.file_frame_count() == fileFrames, "the frames of the file at its own rate");
    check(qint64(reader.output_frame_count()) == TTimeRef::frame_at_or_after(TTimeRef(fileFrames, fileRate), outputRate),
          "the frames at the output rate end where the file ends");

    const nframes_t bufferSize = 1024;
    const nframes_t slotCount = std::max<nframes_t>(5, outputRate / bufferSize);
    const nframes_t total = nframes_t(double(reader.output_frame_count()) / bufferSize);
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
            std::ignore = reader.read(buffer, location, frames);
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

struct Read {
    TTimeRef location;
    nframes_t count;
};

static std::vector<float> read_sequence(TAudioReader& reader, const std::vector<Read>& reads)
{
    std::vector<float> out;
    TFileIOBuffer buffer;
    for (const Read& r : reads) {
        std::ignore = reader.read(buffer, r.location, r.count);
        const float* data = buffer.get_channel_buffer(0).get_data(r.count);
        out.insert(out.end(), data, data + r.count);
    }
    return out;
}

// The reads of TBufferedAudioStreamReader after a seek before a clip: the
// clip starts modulus into slot 0, its first frame goes to slot position
// offset = frame_nearest(modulus), the first read fills the rest of that
// slot (rb_seek_to_transport_location()), the refills whole slots
// (fill_realtime_buffers()). onSlots: the first read at the time of slot
// position offset, as every refill reads at the time of its slot; else at
// the source start, as before 2026-10-25.
static std::vector<Read> ring_reads(const TTimeRef& sourceStart, const TTimeRef& modulus, uint rate, bool onSlots)
{
    const nframes_t bufferSize = 1024;
    const TTimeRef slotStart = sourceStart - modulus;
    const nframes_t offset = TTimeRef::to_nframes(TTimeRef::frame_nearest(modulus, rate));

    std::vector<Read> reads;
    reads.push_back({onSlots ? slotStart + TTimeRef(offset, rate) : sourceStart, bufferSize - offset});
    nframes_t slot = 1;
    for (const nframes_t refill : {nframes_t(7), nframes_t(3), nframes_t(5)}) {
        reads.push_back({slotStart + TTimeRef(slot * bufferSize, rate), refill * bufferSize});
        slot += refill;
    }
    return reads;
}

// The largest difference between the reads and one read from the frame of
// the first read on, by a reader of its own
static float off_one_read(const QString& path, uint rate, TAudioResampler::BackendType backend, const std::vector<Read>& reads)
{
    const std::unique_ptr<TAudioReader> ringReader = TAudioReader::open(path).value_or(nullptr);
    const std::unique_ptr<TAudioReader> oneReader = TAudioReader::open(path).value_or(nullptr);
    std::ignore = ringReader->set_output_rate_and_resampler(rate, backend, TAudioResampler::default_realtime_quality(backend));
    std::ignore = oneReader->set_output_rate_and_resampler(rate, backend, TAudioResampler::default_realtime_quality(backend));

    const std::vector<float> ring = read_sequence(*ringReader, reads);
    const nframes_t first = TTimeRef::to_nframes(TTimeRef::frame_nearest(reads.front().location, rate));
    const std::vector<float> one = read_sequence(*oneReader, {{TTimeRef(first, rate), nframes_t(ring.size())}});

    float worst = 0.0f;
    for (size_t i = 0; i < ring.size(); ++i) {
        worst = std::max(worst, std::fabs(ring[i] - one[i]));
    }
    return worst;
}

// The ring's reads join, bit for bit one read, whatever fraction of a frame
// the source start and the clip's place in its slot have. Their first read
// at the source start, off the slots' grid, doubles a frame where the
// fractions fall wrong (0.6 and 0.3): that must show.
static void ring_reads_join(const QTemporaryDir& dir)
{
    struct Case { uint fileRate; uint outputRate; };
    for (const Case& c : {Case{44100, 48000}, Case{48000, 44100}, Case{48000, 48000}}) {
        std::vector<float> tone(size_t(2 * c.fileRate));
        for (size_t i = 0; i < tone.size(); ++i) {
            tone[i] = float(0.5 * std::sin(2.0 * M_PI * 1000.0 * double(i) / c.fileRate));
        }
        const QString path = dir.filePath(QString("ring-%1.wav").arg(c.fileRate));
        check(write_wav(path, c.fileRate, tone), "writing the tone");

        // whole + fraction output frames as a time
        const qint64 span = TTimeRef::UNIVERSAL_SAMPLE_RATE / c.outputRate;
        auto frames = [span](qint64 whole, double fraction) { return TTimeRef(whole * span + std::llround(fraction * double(span))); };

        // Between two frames the nearest, half to the later one (the file
        // as it is, so the frames are the file's samples)
        if (c.fileRate == c.outputRate) {
            const std::unique_ptr<TAudioReader> reader = TAudioReader::open(path).value_or(nullptr);
            bool nearest = true;
            for (const auto& [fraction, frame] : {std::pair{0.4, size_t(1000)}, std::pair{0.5, size_t(1001)}, std::pair{0.6, size_t(1001)}}) {
                const std::vector<float> one = read_sequence(*reader, {{frames(1000, fraction), 1}});
                nearest = nearest && one.front() == tone[frame];
            }
            check(nearest, "a location between two frames reads the nearest frame");
            if (nearest) {
                std::printf("ok:   a location between two frames reads the nearest frame (1000.4, 1000.5, 1000.6)\n");
            }
        }

        // The fractions of the source start and of the clip's place in its slot
        const std::pair<double, double> fractions[] = {{0.0, 0.0}, {0.0, 0.3}, {0.0, 0.7}, {0.6, 0.3}, {0.4, 0.6}, {0.5, 0.5}, {0.3, 0.5}};

        for (TAudioResampler::BackendType backend : {TAudioResampler::BackendType::LIBSOXR, TAudioResampler::BackendType::LIBSAMPLERATE}) {
            for (const auto& [sourceFraction, modulusFraction] : fractions) {
                const float worst = off_one_read(path, c.outputRate, backend,
                                                 ring_reads(frames(1000, sourceFraction), frames(300, modulusFraction), c.outputRate, true));
                char what[200];
                std::snprintf(what, sizeof(what), "%u -> %u Hz, %s: the ring's reads, source start 1000%+.1f, clip %.1f into its slot, are one read (%.7f off)",
                              c.fileRate, c.outputRate, qPrintable(TAudioResampler::backend_name(backend)), sourceFraction,
                              300 + modulusFraction, double(worst));
                check(worst == 0.0f, what);
                if (worst == 0.0f) {
                    std::printf("ok:   %s\n", what);
                }
            }

            const float before = off_one_read(path, c.outputRate, backend, ring_reads(frames(1000, 0.6), frames(300, 0.3), c.outputRate, false));
            char what[200];
            std::snprintf(what, sizeof(what), "%u -> %u Hz, %s: the first read at the source start (1000.6, clip 300.3 into its slot) doubles a frame (%.5f off)",
                          c.fileRate, c.outputRate, qPrintable(TAudioResampler::backend_name(backend)), double(before));
            check(before > 0.01f, what);
            if (before > 0.01f) {
                std::printf("ok:   %s\n", what);
            }
        }
    }
}

int main()
{
    QTemporaryDir dir;
    check(dir.isValid(), "a temporary directory");

    ring_reads_join(dir);

    // The common pairs, the lowest and highest rates, the largest drop
    // after a seek (11025 -> 64000: 2559 frames), and equal rates, which
    // read the file as it is
    struct Case { uint fileRate; uint outputRate; };
    const Case cases[] = {{44100, 48000}, {48000, 44100}, {96000, 48000}, {22050, 48000}, {44100, 96000},
                          {88200, 48000}, {8000, 48000}, {192000, 44100}, {11025, 64000}, {48000, 48000}};
    const TAudioResampler::BackendType backends[] = {TAudioResampler::BackendType::LIBSOXR,
                                                     TAudioResampler::BackendType::LIBSAMPLERATE};

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
        // Every quality of every backend
        for (TAudioResampler::BackendType backend : backends) {
        for (int quality : TAudioResampler::qualities(backend)) {
            for (Pattern pattern : {Pattern::Buffers, Pattern::Stream, Pattern::Seek, Pattern::SeekCommon}) {
                nframes_t first = 0;
                const std::vector<float> out = read_all(path, c.outputRate, backend, quality, pattern, first, nframes_t(file.size()), c.fileRate);
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
                std::snprintf(what, sizeof(what), "%u -> %u Hz, %s %s, %s: every sweep in place (worst %+.2f frames, %d sweeps)",
                              c.fileRate, c.outputRate, qPrintable(TAudioResampler::backend_name(backend)),
                              qPrintable(TAudioResampler::quality_name(backend, quality)), pattern_name(pattern), worst, found);
                const bool ok = found >= SweepCount - 4 && std::fabs(worst) <= 0.05;
                check(ok, what);
                if (ok) {
                    std::printf("ok:   %s\n", what);
                }
            }
        }
        }
    }

    // The end of the file: a sweep that ends 3 ms before it is whole in the
    // output, through the flush of the resampler, read in one go or in
    // pieces of 100 frames (the flush goes on over several reads)
    for (TAudioResampler::BackendType backend : backends) {
    for (const nframes_t piece : {nframes_t(0), nframes_t(100)}) {
    for (const Case& c : {Case{44100, 48000}, Case{48000, 44100}, Case{96000, 48000}}) {
        const std::vector<float> s = sweep(c.fileRate, 6000.0);
        const nframes_t fileFrames = nframes_t(std::lround((1.0 + SweepSeconds + 0.003) * c.fileRate));
        std::vector<float> file(fileFrames, 0.0f);
        std::copy(s.begin(), s.end(), file.begin() + long(c.fileRate));
        const QString path = dir.filePath(QString("end-%1.wav").arg(c.fileRate));
        check(write_wav(path, c.fileRate, file), "writing the file");

        const std::unique_ptr<TAudioReader> opened = TAudioReader::open(path).value_or(nullptr);
        check(opened != nullptr, "the file opens");
        if (!opened) {
            continue;
        }
        TAudioReader& reader = *opened;
        std::ignore = reader.set_output_rate_and_resampler(c.outputRate, backend, TAudioResampler::default_realtime_quality(backend));
        const nframes_t frames = reader.output_frame_count();
        TFileIOBuffer buffer;
        std::vector<float> out;
        for (nframes_t at = 0; at < frames;) {
            const nframes_t n = piece ? std::min(piece, frames - at) : frames;
            std::ignore = reader.read(buffer, TTimeRef(at, c.outputRate), n);
            const float* data = buffer.get_channel_buffer(0).get_data(n);
            out.insert(out.end(), data, data + n);
            at += n;
        }
        const std::vector<float> tpl = sweep(c.outputRate, 6000.0);
        const double offset = sweep_offset(out, double(c.outputRate), tpl, 100);
        // Whole, not only found: a sweep cut short by silence matches less
        const double whole = sweep_level(out, size_t(c.outputRate), tpl);

        char what[200];
        std::snprintf(what, sizeof(what), "%u -> %u Hz, %s, %s: the sweep 3 ms before the end in place and whole (%+.2f frames, %.3f)",
                      c.fileRate, c.outputRate, qPrintable(TAudioResampler::backend_name(backend)), piece ? "in pieces" : "in one go",
                      offset, whole);
        const bool ok = !std::isnan(offset) && std::fabs(offset) <= 0.05 && whole > 0.99;
        check(ok, what);
        if (ok) {
            std::printf("ok:   %s\n", what);
        }

        // A seek into the last frames, where more frames are to be dropped
        // after the common frame than the file has left: the read gives
        // what is left and ends. A tone up to the end, so that frames
        // dropped too many or silence in their place show: the read must
        // match the same frames read from the start. Not where every
        // output frame is a common one (96 -> 48 kHz).
        const uint common = std::gcd(c.fileRate, c.outputRate);
        const nframes_t period = c.outputRate / common;
        if (period < 2) {
            continue;
        }
        std::vector<float> tone(c.fileRate / 2 + 70);
        for (size_t i = 0; i < tone.size(); ++i) {
            tone[i] = float(0.5 * std::sin(2.0 * M_PI * 1000.0 * double(i) / c.fileRate));
        }
        const QString tonePath = dir.filePath(QString("tone-%1.wav").arg(c.fileRate));
        check(write_wav(tonePath, c.fileRate, tone), "writing the tone");
        const std::unique_ptr<TAudioReader> toneOpened = TAudioReader::open(tonePath).value_or(nullptr);
        check(toneOpened != nullptr, "the tone opens");
        if (!toneOpened) {
            continue;
        }
        TAudioReader& toneReader = *toneOpened;
        std::ignore = toneReader.set_output_rate_and_resampler(c.outputRate, backend, TAudioResampler::default_realtime_quality(backend));
        const nframes_t toneFrames = toneReader.output_frame_count();
        std::ignore = toneReader.read(buffer, TTimeRef(), toneFrames);
        const std::vector<float> sequential(buffer.get_channel_buffer(0).get_data(toneFrames),
                                            buffer.get_channel_buffer(0).get_data(toneFrames) + toneFrames);

        nframes_t last = (toneFrames - 1) / period * period + period - 1;
        if (last >= toneFrames) {
            last -= period;
        }
        const nframes_t left = toneFrames - last;
        const nframes_t read = toneReader.read(buffer, TTimeRef(last, c.outputRate), left).frames;
        const float* data = buffer.get_channel_buffer(0).get_data(left);
        float worstDiff = 0.0f;
        for (nframes_t i = 0; i < left; ++i) {
            worstDiff = std::max(worstDiff, std::fabs(data[i] - sequential[last + i]));
        }
        std::snprintf(what, sizeof(what), "%u -> %u Hz, %s, %s: a seek %u frames before the end, %u to drop, reads them (%u, %.5f off)",
                      c.fileRate, c.outputRate, qPrintable(TAudioResampler::backend_name(backend)),
                      piece ? "after pieces" : "after one go", left, period - 1, read, double(worstDiff));
        const bool lastOk = left < period - 1 && read == left && worstDiff < 0.01f;
        check(lastOk, what);
        if (lastOk) {
            std::printf("ok:   %s\n", what);
        }
        // Both ways on one reader (one reader per use is the rule): a read of
        // the file's own frames between two reads at the output rate gives
        // the file's samples, and the second read goes on at its frame: it
        // seeks, its resampler starts anew (no frame lost or doubled; a
        // frame off is some 0.07 for this tone)
        {
            const std::unique_ptr<TAudioReader> bothOpened = TAudioReader::open(tonePath).value_or(nullptr);
            TAudioReader& both = *bothOpened;
            std::ignore = both.set_output_rate_and_resampler(c.outputRate, backend, TAudioResampler::default_realtime_quality(backend));
            const nframes_t half = toneFrames / 2;
            TFileIOBuffer output;
            std::ignore = both.read(output, TTimeRef(), half);
            std::vector<float> resampled(output.get_channel_buffer(0).get_data(half), output.get_channel_buffer(0).get_data(half) + half);
            TFileIOBuffer native;
            const nframes_t nativeFirst = nframes_t(tone.size() / 3);
            const nframes_t nativeRead = both.read_file_frames(native, nativeFirst, 500).frames;
            float nativeDiff = 0.0f;
            for (nframes_t i = 0; i < nativeRead; ++i) {
                nativeDiff = std::max(nativeDiff, std::fabs(native.get_channel_buffer(0).get_data(500)[i] - tone[nativeFirst + i]));
            }
            const nframes_t rest = toneFrames - half;
            std::ignore = both.read(output, TTimeRef(half, c.outputRate), rest);
            resampled.insert(resampled.end(), output.get_channel_buffer(0).get_data(rest), output.get_channel_buffer(0).get_data(rest) + rest);
            float outputDiff = 0.0f;
            for (nframes_t i = 0; i < toneFrames; ++i) {
                outputDiff = std::max(outputDiff, std::fabs(resampled[i] - sequential[i]));
            }
            std::snprintf(what, sizeof(what), "%u -> %u Hz, %s, %s: the file's own frames between two reads at the output rate (%u, %.5f; %.5f off)",
                          c.fileRate, c.outputRate, qPrintable(TAudioResampler::backend_name(backend)), piece ? "after pieces" : "after one go",
                          nativeRead, double(nativeDiff), double(outputDiff));
            const bool bothOk = nativeRead == 500 && nativeDiff < 1.0e-6f && outputDiff < 1.0e-3f;
            check(bothOk, what);
            if (bothOk) {
                std::printf("ok:   %s\n", what);
            }
        }
        // A new output rate leaves the reader without a position: a read
        // after it at a location reads there as a new reader does, also where
        // that location's frame at the new rate is the frame the reader was
        // at before (1000)
        {
            const uint otherRate = c.outputRate == 48000 ? 44100 : 48000;
            const std::unique_ptr<TAudioReader> changed = TAudioReader::open(tonePath).value_or(nullptr);
            const std::unique_ptr<TAudioReader> fresh = TAudioReader::open(tonePath).value_or(nullptr);
            std::ignore = changed->set_output_rate_and_resampler(c.outputRate, backend, TAudioResampler::default_realtime_quality(backend));
            std::ignore = fresh->set_output_rate_and_resampler(otherRate, backend, TAudioResampler::default_realtime_quality(backend));
            TFileIOBuffer output;
            std::ignore = changed->read(output, TTimeRef(), 1000);
            std::ignore = changed->set_output_rate_and_resampler(otherRate, backend, TAudioResampler::default_realtime_quality(backend));
            std::ignore = changed->read(output, TTimeRef(nframes_t(1000), otherRate), 2000);
            const std::vector<float> afterChange(output.get_channel_buffer(0).get_data(2000), output.get_channel_buffer(0).get_data(2000) + 2000);
            std::ignore = fresh->read(output, TTimeRef(nframes_t(1000), otherRate), 2000);
            float changeDiff = 0.0f;
            for (nframes_t i = 0; i < 2000; ++i) {
                changeDiff = std::max(changeDiff, std::fabs(afterChange[i] - output.get_channel_buffer(0).get_data(2000)[i]));
            }
            std::snprintf(what, sizeof(what), "%u Hz file, %u -> %u Hz output, %s: after a new rate a read reads where it is asked (%.5f off)",
                          c.fileRate, c.outputRate, otherRate, qPrintable(TAudioResampler::backend_name(backend)), double(changeDiff));
            check(changeDiff == 0.0f, what);
            if (changeDiff == 0.0f) {
                std::printf("ok:   %s\n", what);
            }
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
