// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of the peak pyramid, the peak file, its stream and the
// waveform columns.
// Everything is compared with brute force over the samples, computed in a
// different way than the code under test (per sample instead of per column,
// 128-bit integer time math instead of TTimeRef).

#include "TPeakFile.h"
#include "TPeakPyramid.h"
#include "TPeakStream.h"
#include "TWaveformColumns.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <random>
#include <thread>
#include <vector>

static int s_failures = 0;
static qint64 s_checkedColumns[3] = {};   // per TWaveformColumns::Source

#define CHECK(condition, ...) \
    do { \
        if (!(condition)) { \
            if (++s_failures <= 20) { \
                std::printf("FAIL %s:%d: %s: ", __FILE__, __LINE__, #condition); \
                std::printf(__VA_ARGS__); \
                std::printf("\n"); \
            } \
        } \
    } while (0)

using Int128 = __int128;
static constexpr qint64 U = TTimeRef::UNIVERSAL_SAMPLE_RATE;
static const uint s_rates[] = {8000, 11025, 22050, 44100, 48000, 88200, 96000, 192000, 384000};

static std::mt19937_64 s_random(20261017);

static qint64 random_between(qint64 low, qint64 high)
{
    return std::uniform_int_distribution<qint64>(low, high)(s_random);
}

static double random_unit()
{
    return std::uniform_real_distribution<double>(0.0, 1.0)(s_random);
}

static Int128 floor_div(Int128 numerator, Int128 denominator)
{
    Int128 quotient = numerator / denominator;
    if ((numerator % denominator != 0) && ((numerator < 0) != (denominator < 0))) {
        --quotient;
    }
    return quotient;
}

// A test signal: noise, a sine, spikes past full scale (some past the code
// range), and values that are codes exactly
struct Signal
{
    uint rate;
    std::vector<std::vector<float>> channels;

    qint64 frames() const { return channels.empty() ? 0 : qint64(channels.front().size()); }
};

static Signal make_signal(uint rate, int channelCount, qint64 frames)
{
    Signal signal{rate, std::vector<std::vector<float>>(size_t(channelCount), std::vector<float>(size_t(frames)))};
    for (int c = 0; c < channelCount; ++c) {
        const double frequency = 20.0 + 5000.0 * random_unit();
        const double noise = random_unit() * 0.5;
        for (qint64 n = 0; n < frames; ++n) {
            double value = 0.6 * std::sin(2.0 * M_PI * frequency * double(n) / rate) + noise * (random_unit() * 2.0 - 1.0);
            const double kind = random_unit();
            if (kind < 0.001) {
                value = (random_unit() * 2.0 - 1.0) * 20.0;   // past +-16 (+24 dB): saturated
            } else if (kind < 0.01) {
                value = TPeakLevels::to_value(qint16(random_between(-TPeakLevels::MaxCode, TPeakLevels::MaxCode)));
            }
            signal.channels[size_t(c)][size_t(n)] = float(value);
        }
    }
    return signal;
}

static QTemporaryDir* s_dir = nullptr;
static int s_files = 0;

// An audio file for a peak file to belong to (its size and time)
static QString make_audio_file()
{
    const QString path = s_dir->filePath(QStringLiteral("%1.wav").arg(++s_files));
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly) && file.write("audio") == 5, "write %s", qPrintable(path));
    return path;
}

// Appends signal to stream, in random chunks or in one go
static void append(TPeakStream& stream, const Signal& signal, bool randomChunks, qint64 from = 0, qint64 to = -1)
{
    qint64 position = from;
    const qint64 end = to < 0 ? signal.frames() : to;
    while (position < end) {
        const qint64 count = std::min(randomChunks ? random_between(1, 3000) : end, end - position);
        std::vector<const float*> pointers;
        for (const auto& channel : signal.channels) {
            pointers.push_back(channel.data() + position);
        }
        stream.append(pointers.data(), count);
        position += count;
    }
}

// The pyramid of signal, made the way every peak file is made: level 0
// appended to a TPeakStream, post-processed, then opened. No levels for a
// signal without frames (there is no file then).
struct Built
{
    QString path;
    QString audioPath;
    TPeakFile::SourceInfo source;
    TPeakFile file;
    const TPeakLevels& levels() const { return file.levels(); }
};

static std::unique_ptr<Built> build(const Signal& signal, bool randomChunks)
{
    auto built = std::make_unique<Built>();
    built->audioPath = make_audio_file();
    built->path = s_dir->filePath(QStringLiteral("%1.peaks").arg(++s_files));
    TPeakStream stream(signal.rate, int(signal.channels.size()), built->path);
    CHECK(stream.open() == TPeakFile::Status::Ok, "open the stream");
    append(stream, signal, randomChunks);
    if (stream.finish(built->audioPath) != TPeakFile::Status::Ok) {
        CHECK(signal.frames() == 0, "finish: %s", qPrintable(stream.error()));
        return built;
    }
    while (stream.post_process()) {
    }
    CHECK(stream.published(), "post-processing: %s", qPrintable(stream.error()));
    built->source = TPeakFile::SourceInfo::from_file(built->audioPath, signal.rate, int(signal.channels.size()), signal.frames());
    CHECK(built->file.open(built->path, built->source) == TPeakFile::Status::Ok, "open %s", qPrintable(built->path));
    return built;
}

// Brute force: the quantized min/max of the frames [first, last)
static TPeakBlock reference_block(const std::vector<float>& samples, qint64 first, qint64 last)
{
    float low = std::numeric_limits<float>::max();
    float high = std::numeric_limits<float>::lowest();
    for (qint64 n = first; n < last; ++n) {
        low = std::min(low, samples[size_t(n)]);
        high = std::max(high, samples[size_t(n)]);
    }
    return {TPeakLevels::code_at_or_below(low), TPeakLevels::code_at_or_above(high)};
}

static bool same_levels(const TPeakLevels& a, const TPeakLevels& b)
{
    if (a.level_count() != b.level_count() || a.channel_count() != b.channel_count()
            || a.frame_count() != b.frame_count() || a.rate() != b.rate()) {
        return false;
    }
    for (int level = 0; level < a.level_count(); ++level) {
        if (a.block_count(level) != b.block_count(level)) {
            return false;
        }
        for (int c = 0; c < a.channel_count(); ++c) {
            for (qint64 k = 0; k < a.block_count(level); ++k) {
                const TPeakBlock x = a.block(level, c, k);
                const TPeakBlock y = b.block(level, c, k);
                if (x.min != y.min || x.max != y.max || a.rms(level, c, k) != b.rms(level, c, k)) {
                    return false;
                }
            }
        }
    }
    return true;
}


// The value codes: rounded outwards at every level, monotonic, exact at the
// octaves, saturated beyond the range
static void test_codes()
{
    float previous = -std::numeric_limits<float>::max();
    for (int code = -TPeakLevels::MaxCode; code <= TPeakLevels::MaxCode; ++code) {
        const float value = TPeakLevels::to_value(qint16(code));
        CHECK(value > previous || code == -TPeakLevels::MaxCode, "code %d: %g not above %g", code, value, previous);
        previous = value;
        // A code's own value maps back to it
        CHECK(TPeakLevels::code_at_or_above(value) == code && TPeakLevels::code_at_or_below(value) == code,
              "code %d: value %g maps to %d / %d", code, value, TPeakLevels::code_at_or_above(value), TPeakLevels::code_at_or_below(value));
    }
    CHECK(TPeakLevels::to_value(qint16(TPeakLevels::OneCode)) == 1.0f, "1.0");
    CHECK(TPeakLevels::to_value(qint16(TPeakLevels::OneCode + TPeakLevels::CodesPerOctave)) == 2.0f, "2.0");
    CHECK(TPeakLevels::to_value(qint16(TPeakLevels::OneCode - 3 * TPeakLevels::CodesPerOctave)) == 0.125f, "0.125");
    CHECK(TPeakLevels::to_value(qint16(-32768)) == -TPeakLevels::max_value(), "-32768 is clamped");

    // A log sweep from -200 dB to +30 dB (beyond the -180 asked for), both
    // signs: outwards, by at most one step
    const double step = std::pow(2.0, 1.0 / TPeakLevels::CodesPerOctave);
    for (int i = 0; i <= 230000; ++i) {
        const float magnitude = float(std::pow(10.0, (-200.0 + i * 0.001) / 20.0));
        for (const float value : {magnitude, -magnitude}) {
            const float above = TPeakLevels::to_value(TPeakLevels::code_at_or_above(value));
            const float below = TPeakLevels::to_value(TPeakLevels::code_at_or_below(value));
            if (std::fabs(value) > TPeakLevels::max_value()) {
                CHECK(std::fabs(above) == TPeakLevels::max_value() || std::fabs(below) == TPeakLevels::max_value(),
                      "%g: not saturated", value);
                continue;
            }
            CHECK(above >= value && below <= value, "%g: %g .. %g does not contain it", value, below, above);
            // Within one step, except below the smallest code (silence there)
            if (std::fabs(value) > TPeakLevels::to_value(2)) {
                // The tables are floats: a ratio is exact to about 1e-7
                CHECK(std::fabs(above) <= std::fabs(below) * step * 1.000001 || above == below,
                      "%g: %g .. %g wider than a step", value, below, above);
            }
        }
    }
    // Around the powers of two, where the octave changes: just below, at and
    // just above 2^k, as a max (up) and a min (down)
    for (int k = -27; k <= 3; ++k) {
        const float power = std::ldexp(1.0f, k);
        for (const float value : {std::nextafter(power, 0.0f), power, std::nextafter(power, 16.0f)}) {
            for (const float signedValue : {value, -value}) {
                const qint16 above = TPeakLevels::code_at_or_above(signedValue);
                const qint16 below = TPeakLevels::code_at_or_below(signedValue);
                CHECK(TPeakLevels::to_value(above) >= signedValue && TPeakLevels::to_value(below) <= signedValue
                      && above - below <= 1, "2^%d: %g gives %d .. %d", k, signedValue, below, above);
            }
        }
        CHECK(TPeakLevels::code_at_or_above(power) == TPeakLevels::OneCode + k * TPeakLevels::CodesPerOctave, "2^%d is a code", k);
    }
    // Nearest: one of the two neighbours, the nearer one (silence only below
    // half the smallest code), and within half a step inside the range
    for (int i = 0; i <= 230000; ++i) {
        const float value = float(std::pow(10.0, (-200.0 + i * 0.001) / 20.0));
        const qint16 code = TPeakLevels::code_nearest(value);
        const qint16 below = TPeakLevels::code_at_or_below(value);
        const qint16 above = TPeakLevels::code_at_or_above(value);
        const double distance = std::fabs(double(TPeakLevels::to_value(code)) - value);
        CHECK((code == below || code == above) && (code != 0 || value <= TPeakLevels::to_value(1) * 0.5f)
              && distance <= std::min(std::fabs(double(TPeakLevels::to_value(below)) - value),
                                      std::fabs(double(TPeakLevels::to_value(above)) - value)),
              "nearest of %g is %d (%d .. %d)", value, code, below, above);
        if (value > TPeakLevels::to_value(2) && value < TPeakLevels::max_value()) {
            CHECK(distance <= value * (step - 1.0) * 0.5 * 1.0001, "nearest of %g is %g", value, TPeakLevels::to_value(code));
        }
    }
    CHECK(TPeakLevels::code_nearest(std::ldexp(1.0f, -28)) == 1, "nearest of 2^-28");
    CHECK(TPeakLevels::code_nearest(0.0f) == 0 && TPeakLevels::code_nearest(std::nanf("")) == 0, "nearest of silence");
    CHECK(TPeakLevels::code_at_or_above(0.0f) == 0 && TPeakLevels::code_at_or_below(0.0f) == 0, "zero");
    // Only 0.0 is silence: a value outward from zero gets at least one step,
    // also below the range (a min rounds down, a max up)
    CHECK(TPeakLevels::code_at_or_above(1e-30f) == 1 && TPeakLevels::code_at_or_below(1e-30f) == 0, "below the range");
    CHECK(TPeakLevels::code_at_or_below(-1e-30f) == -1 && TPeakLevels::code_at_or_above(-1e-30f) == 0, "below the range, negative");
    CHECK(TPeakLevels::code_at_or_above(std::numeric_limits<float>::denorm_min()) == 1, "the smallest float");
    CHECK(TPeakLevels::code_at_or_above(20.0f) == TPeakLevels::MaxCode && TPeakLevels::code_at_or_below(-20.0f) == -TPeakLevels::MaxCode,
          "saturation");
    CHECK(TPeakLevels::code_at_or_above(std::nanf("")) == 0 && TPeakLevels::code_at_or_below(std::nanf("")) == 0, "NaN");
    CHECK(TPeakLevels::code_at_or_above(std::numeric_limits<float>::infinity()) == TPeakLevels::MaxCode, "infinity");
}

static void test_time_conversions()
{
    for (int round = 0; round < 200000; ++round) {
        const uint rate = s_rates[random_between(0, std::size(s_rates) - 1)];
        const qint64 day = 24 * TTimeRef::ONE_HOUR_UNIVERSAL_SAMPLE_RATE;
        const qint64 universal = random_between(-day, day);
        double fraction;
        const qint64 before = TTimeRef::frame_at_or_before(TTimeRef(universal), rate, &fraction);
        const qint64 after = TTimeRef::frame_at_or_after(TTimeRef(universal), rate);

        const Int128 scaled = Int128(universal) * rate;
        const Int128 expectedBefore = floor_div(scaled, U);
        const Int128 remainder = scaled - expectedBefore * U;
        CHECK(before == qint64(expectedBefore), "u %lld rate %u", (long long)universal, rate);
        CHECK(after == qint64(remainder == 0 ? expectedBefore : expectedBefore + 1), "u %lld rate %u", (long long)universal, rate);
        CHECK(std::abs(fraction - double(remainder) / double(U)) < 1e-12 && fraction >= 0.0 && fraction < 1.0,
              "u %lld rate %u fraction %f", (long long)universal, rate, fraction);
    }
}

static void test_pyramid()
{
    std::vector<qint64> lengths = {0, 1, 2, 63, 64, 65, 127, 128, 129, 191, 192, 193, 4095, 4096, 4097, 7919, 104729};
    lengths.push_back(random_between(500000, 1100000));

    for (qint64 frames : lengths) {
        for (int channelCount = 1; channelCount <= 3; ++channelCount) {
            const Signal signal = make_signal(s_rates[random_between(0, std::size(s_rates) - 1)], channelCount, frames);
            const std::unique_ptr<Built> whole = build(signal, false);
            // Appended in chunks of any size: the same pyramid
            const std::unique_ptr<Built> chunked = build(signal, true);
            const TPeakLevels& levels = chunked->levels();

            CHECK(same_levels(whole->levels(), levels), "frames %lld: chunked differs from one append", (long long)frames);
            CHECK(levels.frame_count() == frames, "frames %lld", (long long)frames);

            // Level structure: ceil halving down to one block
            qint64 expected = (frames + 63) / 64;
            int expectedLevels = 0;
            for (qint64 blocks = expected; blocks > 0; blocks = blocks > 1 ? (blocks + 1) / 2 : 0) {
                ++expectedLevels;
            }
            CHECK(levels.level_count() == expectedLevels, "frames %lld: %d levels, expected %d",
                  (long long)frames, levels.level_count(), expectedLevels);

            for (int level = 0; level < levels.level_count(); ++level) {
                CHECK(levels.block_count(level) == expected, "frames %lld level %d: %lld blocks, expected %lld",
                      (long long)frames, level, (long long)levels.block_count(level), (long long)expected);
                const qint64 size = qint64(TPeakLevels::BlockFrames) << level;
                for (int c = 0; c < channelCount && levels.block_count(level) == expected; ++c) {
                    for (qint64 k = 0; k < expected; ++k) {
                        const TPeakBlock reference = reference_block(signal.channels[size_t(c)], k * size, std::min(frames, (k + 1) * size));
                        const TPeakBlock block = levels.block(level, c, k);
                        CHECK(block.min == reference.min && block.max == reference.max,
                              "frames %lld level %d channel %d block %lld: %d..%d expected %d..%d", (long long)frames, level, c,
                              (long long)k, block.min, block.max, reference.min, reference.max);
                    }
                }
                expected = (expected + 1) / 2;
            }

            // Ranges, on the finished pyramid and on level 0 of a stream that is
            // still written (a view of level 0 only)
            TPeakStream growing(signal.rate, channelCount, s_dir->filePath(QStringLiteral("%1.peaks").arg(++s_files)));
            CHECK(growing.open() == TPeakFile::Status::Ok, "open a growing stream");
            append(growing, signal, true);
            std::vector<qint16> records;
            const TPeakLevels growingLevels = growing.read(0, (frames + 63) / 64, records);
            for (const TPeakLevels* view : {&levels, &growingLevels}) {
                if (view->level_count() == 0 || view->block_count(0) == 0) {
                    continue;
                }
                const qint64 blocks = view->block_count(0);
                for (int round = 0; round < 300; ++round) {
                    const qint64 first = random_between(0, blocks - 1);
                    const qint64 last = random_between(first + 1, blocks);
                    const int c = int(random_between(0, channelCount - 1));
                    // The level-0 blocks are checked against the samples above
                    TPeakBlock reference = levels.block(0, c, first);
                    for (qint64 k = first + 1; k < last; ++k) {
                        reference.min = std::min(reference.min, levels.block(0, c, k).min);
                        reference.max = std::max(reference.max, levels.block(0, c, k).max);
                    }
                    const TPeakBlock range = view->range(c, first, last);
                    CHECK(range.min == reference.min && range.max == reference.max,
                          "frames %lld range %lld..%lld (%s)", (long long)frames, (long long)first, (long long)last,
                          view == &levels ? "finished" : "growing");
                }
            }
        }
    }
}

static QByteArray file_bytes(const QString& path)
{
    QFile file(path);
    CHECK(file.open(QIODevice::ReadOnly), "read %s", qPrintable(path));
    return file.readAll();
}

static void test_file()
{
    // Lengths around the edges of blocks and of level sizes
    for (qint64 frames : {qint64(1), qint64(63), qint64(64), qint64(65), qint64(1000), qint64(64 * 1024 - 1), qint64(64 * 1024),
                          qint64(64 * 1024 + 1), qint64(123457), random_between(1, 2000000)}) {
        const int channelCount = int(random_between(1, 3));
        const Signal signal = make_signal(48000, channelCount, frames);

        // Appended in chunks or in one go: the same records (the headers name
        // audio files of their own)
        const std::unique_ptr<Built> whole = build(signal, false);
        const std::unique_ptr<Built> chunked = build(signal, true);
        CHECK(file_bytes(whole->path).mid(int(TPeakFile::HeaderBytes)) == file_bytes(chunked->path).mid(int(TPeakFile::HeaderBytes)),
              "frames %lld: the files differ", (long long)frames);
        const qint64 records = [&] {
            qint64 sum = 0;
            for (const qint64 count : TPeakFile::block_counts(frames)) {
                sum += count;
            }
            return sum;
        }();
        CHECK(QFileInfo(whole->path).size() == TPeakFile::HeaderBytes + records * TPeakFile::record_bytes(channelCount),
              "frames %lld: file size", (long long)frames);

        // Another source
        const TPeakFile::SourceInfo source = whole->source;
        auto expect_invalid = [&](TPeakFile::SourceInfo other, const char* what) {
            TPeakFile mismatch;
            CHECK(mismatch.open(whole->path, other) == TPeakFile::Status::Invalid && !mismatch.is_open(), "frames %lld: %s accepted",
                  (long long)frames, what);
        };
        TPeakFile::SourceInfo other = source; other.rate = 44100; expect_invalid(other, "rate");
        other = source; other.channelCount = channelCount + 1; expect_invalid(other, "channels");
        other = source; other.frameCount -= 1; expect_invalid(other, "fewer frames");
        other = source; other.fileSize += 1; expect_invalid(other, "source size");
        other = source; other.modified += TPeakFile::SourceTimeTolerance + 1; expect_invalid(other, "source time beyond the tolerance");

        // A decoder that reports more frames than it delivers: the file holds
        // the frames that were really read and is valid for that source
        other = source; other.frameCount += 1000;
        TPeakFile longer;
        CHECK(longer.open(whole->path, other) == TPeakFile::Status::Ok && same_levels(longer.levels(), whole->levels()),
              "frames %lld: over-reported source", (long long)frames);
    }

    // Nothing to write: no file a source could match
    {
        const Signal empty = make_signal(48000, 1, 0);
        const std::unique_ptr<Built> built = build(empty, false);
        CHECK(!QFile::exists(built->path) && built->levels().level_count() == 0, "a peak file without audio");
    }

    // A crash before the header (while level 0 is written, or during
    // post-processing) leaves a part file whose header is zero: refused
    // under the file's name. A stream that is given up removes its part file.
    {
        const Signal signal = make_signal(44100, 2, 200000);
        const QString audio = make_audio_file();
        const QString path = s_dir->filePath(QStringLiteral("%1.peaks").arg(++s_files));
        const TPeakFile::SourceInfo source = TPeakFile::SourceInfo::from_file(audio, 44100, 2, signal.frames());
        TPeakStream stream(44100, 2, path);
        CHECK(stream.open() == TPeakFile::Status::Ok, "open");
        append(stream, signal, true);
        CHECK(!QFile::exists(path) && QFile::exists(stream.part_path()), "level 0 not in its part file");
        auto refused_as_crash = [&](const char* when) {
            CHECK(QFile::copy(stream.part_path(), path), "copy the part file");
            TPeakFile file;
            CHECK(file.open(path, source) == TPeakFile::Status::Invalid, "a file left %s accepted", when);
            QFile::remove(path);
        };
        refused_as_crash("while level 0 is written");
        CHECK(stream.finish(audio) == TPeakFile::Status::Ok, "finish");
        refused_as_crash("after level 0");
        CHECK(stream.post_process(), "post-processing in more than one step");
        refused_as_crash("halfway post-processing");
        // Given up halfway (the project closes): no file, no part file
        stream.abandon();
        CHECK(!stream.post_process() && !stream.published() && !QFile::exists(path) && !QFile::exists(stream.part_path()),
              "a stream given up halfway post-processing left a file");

        // A rename that cannot happen (a directory under the file's name):
        // publish() leaves the part file to its caller, post-processing gives
        // up and removes it (on Windows after trying for a while)
        {
            const QString blocked = s_dir->filePath(QStringLiteral("%1.peaks").arg(++s_files));
            CHECK(QDir().mkpath(blocked + "/inside"), "a directory under the peak file's name");
            TPeakStream renamed(44100, 2, blocked);
            CHECK(renamed.open() == TPeakFile::Status::Ok, "open");
            append(renamed, signal, false);
            CHECK(renamed.finish(audio) == TPeakFile::Status::Ok, "finish");
            const QString part = renamed.part_path();
            CHECK(TPeakFile::publish(part, blocked) != TPeakFile::Status::Ok && QFile::exists(part), "publish() removed the part file");
            int steps = 0;
            while (renamed.post_process() && ++steps < 100000) {
            }
            CHECK(!renamed.published() && renamed.write_failed() && !QFile::exists(part) && QFileInfo(blocked).isDir(),
                  "a refused rename: published %d, part file left %d", renamed.published(), QFile::exists(part));
        }

        // Every stream its own part file: two writers of one peak file (two
        // Traverso instances, an old and a new build) do not share one
        TPeakStream other(44100, 2, path);
        TPeakStream third(44100, 2, path);
        CHECK(other.open() == TPeakFile::Status::Ok && third.open() == TPeakFile::Status::Ok
                  && other.part_path() != third.part_path() && QFile::exists(other.part_path()) && QFile::exists(third.part_path()),
              "two writers of one file share a part file");
        other.abandon();
        CHECK(!QFile::exists(other.part_path()) && QFile::exists(third.part_path()), "giving up one stream removed the other's part file");
    }

    // Damaged files
    const Signal signal = make_signal(44100, 1, 50000);
    const std::unique_ptr<Built> built = build(signal, false);
    const QString path = built->path;
    const TPeakFile::SourceInfo source = built->source;
    built->file.close();
    const QByteArray bytes = file_bytes(path);

    auto rewrite = [&](const QByteArray& content) {
        QFile out(path);
        CHECK(out.open(QIODevice::WriteOnly | QIODevice::Truncate), "rewrite");
        out.write(content);
        out.close();
    };
    // Refused when opening, fields that keep the layout the same size by the
    // header's checksum
    auto expect_damaged = [&](const QByteArray& damaged, const char* what) {
        rewrite(damaged);
        TPeakFile file;
        CHECK(file.open(path, source) == TPeakFile::Status::Invalid, "%s accepted", what);
    };
    auto patched = [&](int offset, quint32 value) {
        QByteArray copy = bytes;
        std::memcpy(copy.data() + offset, &value, sizeof(value));
        return copy;
    };
    expect_damaged(QByteArray(bytes).replace(0, 1, "X"), "magic");
    expect_damaged(patched(8, 2), "version 2 (periods)");
    expect_damaged(patched(8, 4), "version 4");
    expect_damaged(patched(8, 6), "version 6 (development)");
    // Offsets in TPeakFileHeader: 8 version, 12 byte order, 16 header size,
    // 20 rate, 24 channels, 28 block frames, 32 codes per octave, 36 record contents,
    // 40 frame count (TPeakFile.cpp asserts 72 bytes and 40)
    expect_damaged(patched(12, 0x04030201), "byte order");
    expect_damaged(patched(16, 64), "header size");
    expect_damaged(patched(28, 32), "block frames");
    expect_damaged(patched(32, 512), "codes per octave");
    expect_damaged(patched(36, 1), "record contents: min/max only");
    expect_damaged(patched(36, 7), "record contents: one more than this version knows");
    expect_damaged(patched(40, 0), "frame count 0");
    expect_damaged(patched(40, 49000), "frame count with another layout");
    expect_damaged(patched(40, 49999), "frame count with the same layout (header checksum)");
    expect_damaged(QByteArray(int(TPeakFile::HeaderBytes), '\0') + bytes.mid(int(TPeakFile::HeaderBytes)), "a header of zeros (unfinished)");
    expect_damaged(bytes.left(bytes.size() - 6), "truncated file");
    expect_damaged(bytes + QByteArray(6, '\0'), "trailing bytes");
    expect_damaged(bytes.left(40), "truncated header");

    TPeakFile missing;
    CHECK(missing.open(s_dir->filePath("none.peaks"), source) == TPeakFile::Status::Missing, "missing file");
}

class MemorySource : public TWaveformSampleSource
{
public:
    explicit MemorySource(const Signal& signal) : m_signal(signal) {}

    qint64 read(qint64 firstFrame, qint64 count, float* const* channels) override
    {
        const qint64 available = std::max<qint64>(0, std::min(count, m_signal.frames() - firstFrame));
        for (size_t c = 0; c < m_signal.channels.size(); ++c) {
            std::copy_n(m_signal.channels[c].data() + firstFrame, available, channels[c]);
        }
        reads += available;
        return available;
    }

    qint64 reads{0};

private:
    const Signal& m_signal;
};

struct Span
{
    float min = std::numeric_limits<float>::max();
    float max = std::numeric_limits<float>::lowest();

    void add(float value) { min = std::min(min, value); max = std::max(max, value); }
    bool empty() const { return min > max; }
};

// The column a time (in universal frames * rate units) falls in
static qint64 column_of(Int128 scaledTime, qint64 start, qint64 width, uint rate)
{
    return qint64(floor_div(scaledTime - Int128(start) * rate, Int128(width) * rate));
}

static void test_columns()
{
    for (int round = 0; round < 800; ++round) {
        const uint rate = s_rates[random_between(0, std::size(s_rates) - 1)];
        const int channelCount = int(random_between(1, 2));
        const qint64 frames = random_between(0, 4) == 0 ? random_between(0, 200) : random_between(1, 80000);
        const Signal signal = make_signal(rate, channelCount, frames);
        const std::unique_ptr<Built> builder = build(signal, true);
        const TPeakLevels levels = builder->levels();

        // Frames per column, log-uniform from 1/20 to 100000, sometimes right at the threshold
        double framesPerColumn = std::exp(std::log(0.05) + random_unit() * (std::log(100000.0) - std::log(0.05)));
        if (random_between(0, 9) == 0) {
            framesPerColumn = TWaveformColumns::SourceThreshold + double(random_between(-1, 1)) * 1e-3;
        }
        const qint64 width = std::max<qint64>(1, qint64(std::llround(framesPerColumn * double(U) / rate)));
        const int count = int(random_between(0, 600));
        const qint64 sourceSpan = std::max<qint64>(1, frames) * (U / rate + 1);
        const qint64 start = random_between(-qint64(count) * width, sourceSpan + width) - (random_between(0, 1) ? 0 : random_between(0, width));

        MemorySource memory(signal);
        std::vector<TWaveformColumn> out(size_t(count) * size_t(channelCount));
        const TWaveformColumns::Source used = TWaveformColumns::query(levels, &memory, TTimeRef(start), TTimeRef(width), count, out.data());

        const double actualFramesPerColumn = double(width) * rate / double(U);
        const TWaveformColumns::Source expected = frames == 0 ? TWaveformColumns::Source::None
            : actualFramesPerColumn < TWaveformColumns::SourceThreshold ? TWaveformColumns::Source::Samples
            : TWaveformColumns::Source::Pyramid;
        if (used != expected) {
            CHECK(used == expected, "round %d: source %d, expected %d (fpc %f)", round, int(used), int(expected), actualFramesPerColumn);
            continue;
        }
        if (used == TWaveformColumns::Source::None) {
            continue;
        }

        for (int c = 0; c < channelCount; ++c) {
            const std::vector<float>& samples = signal.channels[size_t(c)];
            std::vector<Span> reference(static_cast<size_t>(count));

            if (used == TWaveformColumns::Source::Samples) {
                // Every sample into the column of its time
                for (qint64 n = 0; n < frames; ++n) {
                    const qint64 i = column_of(Int128(n) * U, start, width, rate);
                    if (i >= 0 && i < count) {
                        reference[size_t(i)].add(samples[size_t(n)]);
                    }
                }
                // The line's value at every column edge, into both neighbours
                for (int i = 0; i <= count; ++i) {
                    const Int128 scaled = Int128(start + qint64(i) * width) * rate;   // edge time * rate
                    const Int128 frame = floor_div(scaled, U);
                    const long double fraction = (long double)(scaled - frame * U) / (long double)U;
                    if (frame < 0 || frame >= frames || (fraction > 0 && frame + 1 >= frames)) {
                        continue;
                    }
                    const long double a = samples[size_t(frame)];
                    const float value = fraction > 0 ? float(a + fraction * ((long double)samples[size_t(frame) + 1] - a)) : float(a);
                    if (i > 0) {
                        reference[size_t(i) - 1].add(value);
                    }
                    if (i < count) {
                        reference[size_t(i)].add(value);
                    }
                }
            } else {
                // Every level-0 block into the column of its first frame
                const qint64 blocks = (frames + 63) / 64;
                for (qint64 k = 0; k < blocks; ++k) {
                    const qint64 i = column_of(Int128(k * 64) * U, start, width, rate);
                    if (i >= 0 && i < count) {
                        const TPeakBlock block = reference_block(samples, k * 64, std::min(frames, k * 64 + 64));
                        reference[size_t(i)].add(TPeakLevels::to_value(block.min));
                        reference[size_t(i)].add(TPeakLevels::to_value(block.max));
                    }
                }
            }

            const float tolerance = used == TWaveformColumns::Source::Samples ? 1e-5f : 0.0f;
            for (int i = 0; i < count; ++i) {
                const TWaveformColumn& column = out[size_t(c) * size_t(count) + size_t(i)];
                const Span& span = reference[size_t(i)];
                if (span.empty() || column.is_empty()) {
                    CHECK(span.empty() == column.is_empty(), "round %d %s column %d: empty %d, expected %d (fpc %f)", round,
                          used == TWaveformColumns::Source::Samples ? "samples" : "pyramid", i, column.is_empty(), span.empty(),
                          actualFramesPerColumn);
                    continue;
                }
                ++s_checkedColumns[int(used)];
                CHECK(std::abs(column.min - span.min) <= tolerance && std::abs(column.max - span.max) <= tolerance,
                      "round %d %s column %d: %f..%f expected %f..%f (fpc %f)", round,
                      used == TWaveformColumns::Source::Samples ? "samples" : "pyramid", i, column.min, column.max, span.min,
                      span.max, actualFramesPerColumn);
            }

            if (used == TWaveformColumns::Source::Pyramid) {
                // Against the real samples: every sample is inside its own
                // column or the one before (codes are rounded outwards)
                const float limit = TPeakLevels::max_value();
                for (qint64 n = 0; n < frames; ++n) {
                    const qint64 i = column_of(Int128(n) * U, start, width, rate);
                    if (i < 1 || i >= count) {
                        continue;
                    }
                    const float value = std::clamp(samples[size_t(n)], -limit, limit);
                    bool inside = false;
                    for (qint64 j : {i - 1, i}) {
                        const TWaveformColumn& column = out[size_t(c) * size_t(count) + size_t(j)];
                        inside = inside || (!column.is_empty() && value >= column.min && value <= column.max);
                    }
                    CHECK(inside, "round %d: sample %lld not in column %lld or the one before", round, (long long)n, (long long)i);
                }
            }
        }

        // A line has no gaps: neighbouring sample columns touch
        if (used == TWaveformColumns::Source::Samples) {
            for (int c = 0; c < channelCount; ++c) {
                for (int i = 0; i + 1 < count; ++i) {
                    const TWaveformColumn& left = out[size_t(c) * size_t(count) + size_t(i)];
                    const TWaveformColumn& right = out[size_t(c) * size_t(count) + size_t(i) + 1];
                    if (!left.is_empty() && !right.is_empty()) {
                        CHECK(left.max >= right.min && left.min <= right.max, "round %d column %d: gap between columns", round, i);
                    }
                }
            }
        }
    }
}

// The RMS plane: every stored RMS within one step of the RMS of its frames
// (computed here from the samples), on every level, the partial last block
// included; a sine's RMS where blocks hold whole periods; silence is code 0;
// columns from the samples agree with the samples
static void test_rms()
{
    // The RMS of level 0 is the nearest code: within half a step, plus what
    // float sums of squares lose (about 1e-6). The levels above come from
    // the decoded level-0 codes, rounded once more: within a step.
    const double step = TPeakCodeMath::octave_fraction(1);
    const double half = std::sqrt(step) * 1.00001;
    auto within_a_step = [&](qint16 code, double expected, int level) {
        const double value = TPeakLevels::to_value(code);
        if (expected <= TPeakLevels::to_value(1)) {
            return value <= TPeakLevels::to_value(1);
        }
        const double bound = level == 0 ? half : step * 1.00001;
        return value <= expected * bound && value >= expected / bound;
    };

    auto square = [&](float sample) { return double(sample) * sample; };
    for (qint64 frames : {qint64(1), qint64(63), qint64(64), qint64(65), qint64(1000), qint64(123457), random_between(200000, 400000)}) {
        const int channelCount = int(random_between(1, 2));
        const Signal signal = make_signal(44100, channelCount, frames);
        const std::unique_ptr<Built> builder = build(signal, true);
        const TPeakLevels levels = builder->levels();
        int checked = 0;
        for (int level = 0; level < levels.level_count(); ++level) {
            for (int c = 0; c < channelCount; ++c) {
                for (qint64 k = 0; k < levels.block_count(level); k += 1 + k / 50) {
                    const qint64 first = k << (TPeakLevels::BlockFramesShift + level);
                    const qint64 last = std::min(first + (qint64(TPeakLevels::BlockFrames) << level), frames);
                    double squares = 0.0;
                    for (qint64 n = first; n < last; ++n) {
                        squares += square(signal.channels[size_t(c)][size_t(n)]);
                    }
                    const double expected = std::sqrt(squares / double(last - first));
                    CHECK(levels.block_frames(level, k) == last - first, "frames %lld level %d block %lld: %lld frames", (long long)frames,
                          level, (long long)k, (long long)levels.block_frames(level, k));
                    CHECK(within_a_step(levels.rms(level, c, k), expected, level), "frames %lld level %d block %lld: RMS %.9g expected %.9g",
                          (long long)frames, level, (long long)k, TPeakLevels::to_value(levels.rms(level, c, k)), expected);
                    ++checked;
                }
                // The partial last block in particular
                const qint64 k = levels.block_count(level) - 1;
                const qint64 first = k << (TPeakLevels::BlockFramesShift + level);
                double squares = 0.0;
                for (qint64 n = first; n < frames; ++n) {
                    squares += square(signal.channels[size_t(c)][size_t(n)]);
                }
                CHECK(within_a_step(levels.rms(level, c, k), std::sqrt(squares / double(frames - first)), level),
                      "frames %lld level %d: the last block's RMS", (long long)frames, level);
            }
        }
        CHECK(checked > 0, "nothing checked");
    }

    // A sine of amplitude A with 32 frames per period: every block holds
    // whole periods, so its RMS is A / sqrt(2), and so is a column of one
    // block, on every level
    for (const double amplitude : {1e-6, 0.001, 0.25, 1.0, 3.5}) {
        const qint64 frames = qint64(64) << 12;
        Signal signal{48000, {std::vector<float>(size_t(frames))}};
        for (qint64 n = 0; n < frames; ++n) {
            signal.channels[0][size_t(n)] = float(amplitude * std::sin(2.0 * M_PI * double(n) / 32.0));
        }
        const std::unique_ptr<Built> builder = build(signal, true);
        const TPeakLevels levels = builder->levels();
        const double expected = amplitude / std::sqrt(2.0);
        for (int level = 0; level <= 12; ++level) {
            const qint64 blockFrames = qint64(TPeakLevels::BlockFrames) << level;
            const int count = int(std::min<qint64>(frames / blockFrames, 64));
            std::vector<TWaveformColumn> out(static_cast<size_t>(count));
            const TTimeRef width(uint64_t(blockFrames), signal.rate);
            CHECK(TWaveformColumns::query(levels, nullptr, TTimeRef(), width, count, out.data()) == TWaveformColumns::Source::Pyramid,
                  "sine: pyramid");
            for (int i = 0; i < count; ++i) {
                CHECK(within_a_step(levels.rms(level, 0, i), expected, level), "sine %g level %d block %d: %.9g", amplitude, level, i,
                      TPeakLevels::to_value(levels.rms(level, 0, i)));
                const double column = out[size_t(i)].rms;
                const double bound = level == 0 ? half : step * 1.00001;
                CHECK(column <= expected * bound && column >= expected / bound, "sine %g level %d column %d: %.9g",
                      amplitude, level, i, column);
            }
        }
    }

    // A level-0 block with a NaN or an inf sample gets its peak (clamped to
    // the code range) as its RMS: the blocks above stay finite and right
    {
        Signal signal{48000, {std::vector<float>(65536, 0.1f)}};
        signal.channels[0][1000] = std::nanf("");
        signal.channels[0][30000] = std::numeric_limits<float>::infinity();
        signal.channels[0][50000] = -std::numeric_limits<float>::infinity();
        const std::unique_ptr<Built> builder = build(signal, true);
        const TPeakLevels levels = builder->levels();
        // Level-0 mean squares as the rule says, then weighted up
        std::vector<double> level0(65536 / 64);
        for (size_t k = 0; k < level0.size(); ++k) {
            double squares = 0.0;
            for (size_t n = k * 64; n < k * 64 + 64; ++n) {
                squares += double(signal.channels[0][n]) * signal.channels[0][n];
            }
            const double broken = double(TPeakLevels::max_value());
            level0[k] = std::isnan(squares) ? 0.1 * 0.1 : (std::isinf(squares) ? broken * broken : squares / 64.0);
        }
        for (int level = 0; level < levels.level_count(); ++level) {
            for (qint64 k = 0; k < levels.block_count(level); ++k) {
                double sum = 0.0;
                const qint64 first = k << level;
                const qint64 last = std::min<qint64>(first + (qint64(1) << level), qint64(level0.size()));
                for (qint64 b = first; b < last; ++b) {
                    sum += level0[size_t(b)];
                }
                const double expected = std::sqrt(sum / double(last - first));
                CHECK(std::isfinite(TPeakLevels::to_value(levels.rms(level, 0, k))) && within_a_step(levels.rms(level, 0, k), expected, level),
                      "broken samples: level %d block %lld: RMS %g expected %g", level, (long long)k,
                      TPeakLevels::to_value(levels.rms(level, 0, k)), expected);
            }
        }
    }

    // Silence
    {
        Signal silence{48000, {std::vector<float>(100000, 0.0f), std::vector<float>(100000, 0.0f)}};
        const std::unique_ptr<Built> builder = build(silence, true);
        const TPeakLevels levels = builder->levels();
        for (int level = 0; level < levels.level_count(); ++level) {
            for (qint64 k = 0; k < levels.block_count(level); ++k) {
                CHECK(levels.rms(level, 0, k) == 0 && levels.rms(level, 1, k) == 0, "silence: level %d block %lld", level, (long long)k);
            }
        }
    }

    // Columns from the samples, of exactly 10 frames: the RMS of those frames
    {
        const Signal signal = make_signal(48000, 1, 5000);
        const std::unique_ptr<Built> builder = build(signal, false);
        MemorySource memory(signal);
        std::vector<TWaveformColumn> out(400);
        CHECK(TWaveformColumns::query(builder->levels(), &memory, TTimeRef(), TTimeRef(uint64_t(10), 48000), 400, out.data())
              == TWaveformColumns::Source::Samples, "samples");
        for (int i = 0; i < 400; ++i) {
            double squares = 0.0;
            for (int n = i * 10; n < i * 10 + 10; ++n) {
                squares += double(signal.channels[0][size_t(n)]) * signal.channels[0][size_t(n)];
            }
            const float expected = float(std::sqrt(squares / 10.0));
            CHECK(std::fabs(out[size_t(i)].rms - expected) <= expected * 1e-5f, "samples column %d: RMS %.9g expected %.9g", i,
                  out[size_t(i)].rms, expected);
        }
    }
}

template<typename Test>
static void run(const char* name, Test test)
{
    QElapsedTimer timer;
    timer.start();
    const int failuresBefore = s_failures;
    test();
    std::printf("%-18s %s (%lld ms)\n", name, s_failures == failuresBefore ? "ok" : "FAILED", (long long)timer.elapsed());
}

// One thread appends buffers of 256 frames to a stream (an audio cycle of a
// recording), another reads level 0 below the count meanwhile, as the
// waveform of a recording does: every record read has its final value and
// the count only grows. Once post-processed, the finished file's columns
// equal the ones from level 0 alone: min and max exactly, the RMS within a
// step.
static void test_live_read()
{
    for (qint64 length : {qint64(3000000), qint64(70000) * 64 + 17}) {
        const Signal signal = make_signal(48000, 2, length);
        const std::unique_ptr<Built> reference = build(signal, false);
        const TPeakLevels& final = reference->levels();

        const QString audio = make_audio_file();
        const QString path = s_dir->filePath(QStringLiteral("%1.peaks").arg(++s_files));
        TPeakStream stream(48000, 2, path);
        CHECK(stream.open() == TPeakFile::Status::Ok, "open");
        std::atomic<bool> done{false};
        std::thread writer([&] {
            for (qint64 position = 0; position < length; position += 256) {
                const float* pointers[2] = {signal.channels[0].data() + position, signal.channels[1].data() + position};
                stream.append(pointers, std::min<qint64>(256, length - position));
                if ((position & 8191) == 0) {
                    std::this_thread::yield();
                }
            }
            stream.finish(audio);
            done.store(true);
        });

        int reads = 0;
        qint64 lastFrames = 0;
        bool finished = false;
        std::vector<qint16> records;
        while (!finished) {
            finished = done.load();   // one more read after the writer is done
            const qint64 frames = stream.written_frames();
            CHECK(frames >= lastFrames, "the count went back from %lld to %lld", (long long)lastFrames, (long long)frames);
            CHECK(frames == length || frames % TPeakLevels::BlockFrames == 0, "%lld frames: a partial block before finish()",
                  (long long)frames);
            lastFrames = frames;
            const qint64 blocks = (frames + TPeakLevels::BlockFrames - 1) / TPeakLevels::BlockFrames;
            if (blocks == 0) {
                continue;
            }
            // A window, sometimes past the count: it ends there
            const qint64 first = random_between(0, blocks - 1);
            const qint64 last = random_between(first + 1, blocks + 10);
            const TPeakLevels live = stream.read(first, last, records);
            ++reads;
            CHECK(live.level_count() == 1 && live.frame_count() >= frames && live.block_count(0) >= std::min(last, blocks)
                      && live.block_count(0) <= last, "window %lld..%lld of %lld blocks: %lld", (long long)first, (long long)last,
                  (long long)blocks, (long long)live.block_count(0));
            for (qint64 k = first; k < live.block_count(0); ++k) {
                for (int c = 0; c < 2; ++c) {
                    const TPeakBlock x = live.block(0, c, k);
                    const TPeakBlock y = final.block(0, c, k);
                    CHECK(x.min == y.min && x.max == y.max && live.rms(0, c, k) == final.rms(0, c, k), "block %lld differs", (long long)k);
                }
            }
            if (live.block_count(0) > first) {
                const qint64 a = random_between(first, live.block_count(0) - 1);
                const qint64 b = random_between(a + 1, live.block_count(0));
                const TPeakBlock x = live.range(1, a, b);
                const TPeakBlock y = final.range(1, a, b);
                CHECK(x.min == y.min && x.max == y.max, "range %lld..%lld differs", (long long)a, (long long)b);
            }
        }
        writer.join();
        CHECK(stream.written_frames() == length, "%lld frames written", (long long)stream.written_frames());

        while (stream.post_process()) {
        }
        CHECK(stream.published(), "post-processing: %s", qPrintable(stream.error()));
        TPeakFile file;
        CHECK(file.open(path, TPeakFile::SourceInfo::from_file(audio, 48000, 2, length)) == TPeakFile::Status::Ok
                  && same_levels(file.levels(), final), "the finished file differs from the one built at once");

        // Level 0 alone (still read through the stream, from the file's
        // name now) against the finished file, at any zoom
        const qint64 blocks = (length + 63) / 64;
        const TPeakLevels live = stream.read(0, blocks, records);
        CHECK(live.block_count(0) == blocks, "level 0 after the rename: %lld blocks", (long long)live.block_count(0));
        const double step = TPeakCodeMath::octave_fraction(1) * 1.00001;
        for (int round = 0; round < 40; ++round) {
            const double framesPerColumn = std::exp(std::log(64.0) + random_unit() * (std::log(200000.0) - std::log(64.0)));
            const TTimeRef width(qint64(std::llround(framesPerColumn * double(U) / 48000.0)));
            const int count = int(random_between(1, 300));
            const TTimeRef start(uint64_t(random_between(0, length)), 48000);
            std::vector<TWaveformColumn> a(size_t(count) * 2);
            std::vector<TWaveformColumn> b(size_t(count) * 2);
            CHECK(TWaveformColumns::query(live, nullptr, start, width, count, a.data()) == TWaveformColumns::Source::Pyramid
                      && TWaveformColumns::query(file.levels(), nullptr, start, width, count, b.data()) == TWaveformColumns::Source::Pyramid,
                  "pyramid columns");
            for (size_t i = 0; i < a.size(); ++i) {
                const bool rmsClose = (a[i].rms <= TPeakLevels::to_value(1) && b[i].rms <= TPeakLevels::to_value(1))
                    || (b[i].rms <= a[i].rms * step && b[i].rms >= a[i].rms / step);
                CHECK(a[i].min == b[i].min && a[i].max == b[i].max && rmsClose,
                      "column %zu: live %g..%g rms %g, file %g..%g rms %g", i, a[i].min, a[i].max, a[i].rms, b[i].min, b[i].max, b[i].rms);
            }
        }
        std::printf("    live stream of %lld frames: %d reads while writing\n", (long long)length, reads);
    }
}

// The bound of the real maximum: never below it, at most one code step above
// (when it is inside the code range), exact when samples decide
static bool within_a_step(float amplitude, float expected)
{
    const float step = float(TPeakCodeMath::octave_fraction(1));
    if (expected <= TPeakLevels::to_value(1)) {
        return amplitude >= expected && amplitude <= TPeakLevels::to_value(1);
    }
    return amplitude >= expected && amplitude <= expected * step * 1.000001f;
}

static float brute_max(const Signal& signal, qint64 first, qint64 last)
{
    float expected = 0.0f;
    for (qint64 n = std::max<qint64>(first, 0); n < std::min(last, signal.frames()); ++n) {
        for (const auto& channel : signal.channels) {
            expected = std::max(expected, std::fabs(channel[size_t(n)]));
        }
    }
    return expected;
}

static void test_max_amplitude()
{
    for (int round = 0; round < 400; ++round) {
        const uint rate = s_rates[random_between(0, std::size(s_rates) - 1)];
        const int channelCount = int(random_between(1, 2));
        const qint64 frames = random_between(0, 3) == 0 ? random_between(0, 300) : random_between(1, 60000);
        Signal signal = make_signal(rate, channelCount, frames);
        // Sometimes one clear maximum, sometimes a flat signal with ties
        if (frames > 0 && random_between(0, 2) == 0) {
            signal.channels[0][size_t(random_between(0, frames - 1))] = random_between(0, 1) ? 3.9f : -3.9f;
        }
        const std::unique_ptr<Built> builder = build(signal, true);
        MemorySource memory(signal);

        for (int range = 0; range < 10; ++range) {
            const qint64 first = random_between(-100, frames + 100);
            const qint64 last = random_between(first - 10, frames + 200);
            const float expected = brute_max(signal, first, last);
            memory.reads = 0;
            const float amplitude = TWaveformColumns::max_amplitude(builder->levels(), &memory, first, last);
            // Saturated (the signal has spikes past +24 dB): exact through the
            // samples of those blocks
            if (expected >= TPeakLevels::max_value()) {
                CHECK(amplitude == expected, "round %d range %lld..%lld: saturated %f expected %f", round, (long long)first,
                      (long long)last, amplitude, expected);
                continue;
            }
            CHECK(within_a_step(amplitude, expected), "round %d range %lld..%lld: %.9g expected %.9g", round, (long long)first,
                  (long long)last, amplitude, expected);
            // Only the two partial blocks at the ends are read
            CHECK(memory.reads <= 2 * TPeakLevels::BlockFrames, "round %d: read %lld frames", round, (long long)memory.reads);
        }
    }

    // A hard-limited signal: every block at the ceiling, still nothing but
    // the ends is read
    {
        Signal signal = make_signal(48000, 2, 500000);
        for (auto& channel : signal.channels) {
            for (float& sample : channel) {
                sample = std::clamp(sample * 10.0f, -0.891f, 0.891f);
            }
        }
        const std::unique_ptr<Built> builder = build(signal, false);
        MemorySource memory(signal);
        for (int range = 0; range < 50; ++range) {
            const qint64 first = random_between(0, 250000);
            const qint64 last = random_between(first + 1, 500000);
            memory.reads = 0;
            const float amplitude = TWaveformColumns::max_amplitude(builder->levels(), &memory, first, last);
            CHECK(within_a_step(amplitude, brute_max(signal, first, last)) && memory.reads <= 2 * TPeakLevels::BlockFrames,
                  "limited %lld..%lld: %.9g (read %lld)", (long long)first, (long long)last, amplitude, (long long)memory.reads);
        }
    }

    // A peak just outside the range, in a block the range shares: not counted
    {
        Signal signal{48000, {std::vector<float>(20000)}};
        for (size_t n = 0; n < signal.channels[0].size(); ++n) {
            signal.channels[0][n] = float(0.1 * std::sin(double(n) * 0.05));
        }
        signal.channels[0][64 * 10 + 5] = 0.99f;     // block 10, before the range
        signal.channels[0][64 * 200 + 60] = -0.98f;  // block 200, after it
        const std::unique_ptr<Built> builder = build(signal, false);
        MemorySource memory(signal);
        const qint64 first = 64 * 10 + 20;
        const qint64 last = 64 * 200 + 40;
        const float amplitude = TWaveformColumns::max_amplitude(builder->levels(), &memory, first, last);
        CHECK(within_a_step(amplitude, brute_max(signal, first, last)) && amplitude < 0.9f,
              "a peak outside the range counted: %.9g", amplitude);
    }

    // A float source past +24 dB: the saturated blocks are read, exactly
    {
        Signal signal = make_signal(48000, 2, 100000);
        signal.channels[1][77777] = -30.5f;
        signal.channels[0][1234] = 25.0f;
        const std::unique_ptr<Built> builder = build(signal, false);
        MemorySource memory(signal);
        CHECK(TWaveformColumns::max_amplitude(builder->levels(), &memory, 1000, 99000) == 30.5f, "saturated source");
        CHECK(TWaveformColumns::max_amplitude(builder->levels(), &memory, 1000, 70000) == 25.0f, "saturated source, one block");
    }

    // A signal of 1e-9 (-180 dB), below the code range: still not silence
    {
        Signal signal = make_signal(48000, 1, 10000);
        for (qint64 n = 0; n < signal.frames(); ++n) {
            signal.channels[0][size_t(n)] = float(1e-9 * std::sin(double(n) * 0.01));
        }
        const std::unique_ptr<Built> builder = build(signal, false);
        MemorySource memory(signal);
        const float amplitude = TWaveformColumns::max_amplitude(builder->levels(), &memory, 100, 9000);
        CHECK(amplitude > 0.0f && within_a_step(amplitude, brute_max(signal, 100, 9000)), "1e-9 signal: %g", amplitude);
    }
}

static void test_zero_crossing()
{
    for (int round = 0; round < 300; ++round) {
        const uint rate = s_rates[random_between(0, std::size(s_rates) - 1)];
        const int channelCount = int(random_between(1, 2));
        const qint64 frames = random_between(2, 20000);
        Signal signal = make_signal(rate, channelCount, frames);
        // Sometimes a DC signal without any crossing
        const bool dc = random_between(0, 9) == 0;
        if (dc) {
            for (auto& channel : signal.channels) {
                for (float& v : channel) {
                    v = 0.5f + std::fabs(v) * 0.1f;
                }
            }
        }
        const std::unique_ptr<Built> builder = build(signal, false);
        MemorySource memory(signal);

        for (int probe = 0; probe < 10; ++probe) {
            const qint64 frame = random_between(-50, frames + 50);
            const qint64 maxDistance = random_between(0, 3000);
            const qint64 result = TWaveformColumns::nearest_zero_crossing(builder->levels(), &memory, frame, maxDistance);

            // Brute force: every crossing in the file, pick the nearest
            auto sum = [&](qint64 i) { float v = 0; for (int c = 0; c < channelCount; ++c) v += signal.channels[size_t(c)][size_t(i)]; return v; };
            auto mag = [&](qint64 i) { float v = 0; for (int c = 0; c < channelCount; ++c) v += std::fabs(signal.channels[size_t(c)][size_t(i)]); return v; };
            qint64 expected = -1;
            qint64 expectedDistance = std::numeric_limits<qint64>::max();
            for (qint64 i = 1; i < frames; ++i) {
                const float a = sum(i - 1), b = sum(i);
                if ((a <= 0 && b >= 0) || (a >= 0 && b <= 0)) {
                    const qint64 candidate = mag(i) < mag(i - 1) ? i : i - 1;
                    const qint64 distance = std::abs(candidate - frame);
                    if (distance < expectedDistance) {
                        expectedDistance = distance;
                        expected = candidate;
                    }
                }
            }
            if (expectedDistance > maxDistance) {
                expected = -1;
            }
            // Equal distances on both sides: either is right
            const bool tie = result >= 0 && expected >= 0 && std::abs(result - frame) == std::abs(expected - frame);
            CHECK(result == expected || tie, "round %d frame %lld max %lld: %lld expected %lld", round, (long long)frame,
                  (long long)maxDistance, (long long)result, (long long)expected);
            if (dc) {
                CHECK(result == -1, "round %d: crossing %lld in a DC signal", round, (long long)result);
            }
        }
    }
}

int main()
{
    QTemporaryDir dir;
    CHECK(dir.isValid(), "no temporary directory");
    s_dir = &dir;

    run("codes", test_codes);
    run("time conversions", test_time_conversions);
    run("pyramid", test_pyramid);
    run("file", test_file);
    run("columns", test_columns);
    run("max amplitude", test_max_amplitude);
    run("rms", test_rms);
    run("live read", test_live_read);
    run("zero crossing", test_zero_crossing);
    std::printf("non-empty columns checked: %lld from samples, %lld from the pyramid\n",
                (long long)s_checkedColumns[int(TWaveformColumns::Source::Samples)],
                (long long)s_checkedColumns[int(TWaveformColumns::Source::Pyramid)]);

    if (s_failures) {
        std::printf("%d failures\n", s_failures);
        return 1;
    }
    std::printf("all peak tests passed\n");
    return 0;
}
