// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of the peak pyramid, the peak file and the waveform columns.
// Everything is compared with brute force over the samples, computed in a
// different way than the code under test (per sample instead of per column,
// 128-bit integer time math instead of TTimeRef).

#include "TPeakFile.h"
#include "TPeakPyramid.h"
#include "TWaveformColumns.h"

#include <QElapsedTimer>
#include <QFile>
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

// A test signal: noise, a sine, spikes past full scale, and exact multiples
// of the quantization step
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
                value = (random_unit() * 2.0 - 1.0) * 5.0;   // past +-4.0: clamped
            } else if (kind < 0.01) {
                value = double(random_between(-20000, 20000)) / TPeakLevels::ValueScale;
            }
            signal.channels[size_t(c)][size_t(n)] = float(value);
        }
    }
    return signal;
}

static std::unique_ptr<TPeakPyramidBuilder> build(const Signal& signal, bool randomChunks, bool finish = true)
{
    auto builder = std::make_unique<TPeakPyramidBuilder>(signal.rate, int(signal.channels.size()));
    qint64 position = 0;
    while (position < signal.frames()) {
        const qint64 chunk = randomChunks ? random_between(1, 3000) : signal.frames();
        const qint64 count = std::min(chunk, signal.frames() - position);
        std::vector<const float*> pointers;
        for (const auto& channel : signal.channels) {
            pointers.push_back(channel.data() + position);
        }
        builder->append(pointers.data(), count);
        position += count;
    }
    if (finish) {
        builder->finish();
    }
    return builder;
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
    return {TPeakLevels::quantize_min(low), TPeakLevels::quantize_max(high)};
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
                const TPeakBlock& x = a.block(level, c, k);
                const TPeakBlock& y = b.block(level, c, k);
                if (x.min != y.min || x.max != y.max) {
                    return false;
                }
            }
        }
    }
    return true;
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
            const std::unique_ptr<TPeakPyramidBuilder> whole = build(signal, false);
            const std::unique_ptr<TPeakPyramidBuilder> chunked = build(signal, true);
            const TPeakLevels levels = chunked->levels();

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
                        const TPeakBlock& block = levels.block(level, c, k);
                        CHECK(block.min == reference.min && block.max == reference.max,
                              "frames %lld level %d channel %d block %lld: %d..%d expected %d..%d", (long long)frames, level, c,
                              (long long)k, block.min, block.max, reference.min, reference.max);
                    }
                }
                expected = (expected + 1) / 2;
            }

            // Ranges, on the finished pyramid and on one that is still growing
            const std::unique_ptr<TPeakPyramidBuilder> partial = build(signal, true, false);
            for (const TPeakLevels& view : {levels, partial->levels()}) {
                if (view.level_count() == 0) {
                    continue;
                }
                const qint64 blocks = view.block_count(0);
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
                    const TPeakBlock range = view.range(c, first, last);
                    CHECK(range.min == reference.min && range.max == reference.max,
                          "frames %lld range %lld..%lld (%s)", (long long)frames, (long long)first, (long long)last,
                          view.level_count() == levels.level_count() ? "finished" : "growing");
                }
            }
        }
    }
}

static void test_file()
{
    QTemporaryDir dir;
    CHECK(dir.isValid(), "no temporary directory");
    const QString path = dir.filePath("source.peaks");

    for (qint64 frames : {qint64(0), qint64(1), qint64(64), qint64(1000), qint64(123457)}) {
        const Signal signal = make_signal(48000, 2, frames);
        const std::unique_ptr<TPeakPyramidBuilder> builder = build(signal, true);

        TPeakFile::SourceInfo source{48000, 2, frames, 1234567, 1760000000000};
        QString error;
        CHECK(TPeakFile::write(path, builder->levels(), source, &error) == TPeakFile::Status::Ok, "write: %s", qPrintable(error));

        TPeakFile file;
        CHECK(file.open(path, source) == TPeakFile::Status::Ok, "frames %lld: open", (long long)frames);
        CHECK(same_levels(file.levels(), builder->levels()), "frames %lld: levels differ after reading", (long long)frames);
        file.close();

        // Another source
        auto expect_invalid = [&](TPeakFile::SourceInfo other, const char* what) {
            TPeakFile mismatch;
            CHECK(mismatch.open(path, other) == TPeakFile::Status::Invalid && !mismatch.is_open(), "frames %lld: %s accepted",
                  (long long)frames, what);
        };
        TPeakFile::SourceInfo other = source; other.rate = 44100; expect_invalid(other, "rate");
        other = source; other.channelCount = 1; expect_invalid(other, "channels");
        other = source; other.frameCount += 1; expect_invalid(other, "frames");
        other = source; other.fileSize += 1; expect_invalid(other, "source size");
        other = source; other.modified += 1; expect_invalid(other, "source time");
    }

    // Damaged files
    const Signal signal = make_signal(44100, 1, 50000);
    const std::unique_ptr<TPeakPyramidBuilder> builder = build(signal, false);
    const TPeakFile::SourceInfo source{44100, 1, 50000, 99, 42};
    CHECK(TPeakFile::write(path, builder->levels(), source) == TPeakFile::Status::Ok, "write");

    QFile original(path);
    CHECK(original.open(QIODevice::ReadOnly), "reopen");
    const QByteArray bytes = original.readAll();
    original.close();

    auto expect_damaged = [&](QByteArray damaged, const char* what) {
        QFile out(path);
        CHECK(out.open(QIODevice::WriteOnly | QIODevice::Truncate), "rewrite");
        out.write(damaged);
        out.close();
        TPeakFile file;
        CHECK(file.open(path, source) == TPeakFile::Status::Invalid, "%s accepted", what);
    };
    auto patched = [&](int offset, quint32 value) {
        QByteArray copy = bytes;
        std::memcpy(copy.data() + offset, &value, sizeof(value));
        return copy;
    };
    expect_damaged(QByteArray(bytes).replace(0, 1, "X"), "magic");
    expect_damaged(patched(8, 1), "version");
    expect_damaged(patched(12, 0x04030201), "byte order");
    expect_damaged(patched(28, 32), "block frames");
    expect_damaged(patched(36, 3), "level count");
    expect_damaged(patched(64, 5), "level 0 block count");
    expect_damaged(patched(72, 4), "level 0 offset");
    expect_damaged(bytes.left(bytes.size() - 8), "truncated file");
    expect_damaged(bytes.left(40), "truncated header");

    TPeakFile missing;
    CHECK(missing.open(dir.filePath("none.peaks"), source) == TPeakFile::Status::Missing, "missing file");
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
        const std::unique_ptr<TPeakPyramidBuilder> builder = build(signal, true);
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
                // column or the one before, within one quantization step
                const float step = 1.0f / TPeakLevels::ValueScale;
                for (qint64 n = 0; n < frames; ++n) {
                    const qint64 i = column_of(Int128(n) * U, start, width, rate);
                    if (i < 1 || i >= count) {
                        continue;
                    }
                    const float value = std::clamp(samples[size_t(n)], -4.0f, 4.0f);
                    bool inside = false;
                    for (qint64 j : {i - 1, i}) {
                        const TWaveformColumn& column = out[size_t(c) * size_t(count) + size_t(j)];
                        inside = inside || (!column.is_empty() && value >= column.min - step && value <= column.max + step);
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

template<typename Test>
static void run(const char* name, Test test)
{
    QElapsedTimer timer;
    timer.start();
    const int failuresBefore = s_failures;
    test();
    std::printf("%-18s %s (%lld ms)\n", name, s_failures == failuresBefore ? "ok" : "FAILED", (long long)timer.elapsed());
}

// One thread appends, another takes snapshots meanwhile: every block a
// snapshot names must already have its final value, and ranges over a
// snapshot must agree with the finished pyramid
static void test_growing_pyramid()
{
    const qint64 frames = 3000000;   // 46875 blocks: level 0 spans a chunk boundary (65536) only with more, so also test that
    for (qint64 length : {frames, qint64(70000) * 64 + 17}) {
        const Signal signal = make_signal(48000, 2, length);
        const std::unique_ptr<TPeakPyramidBuilder> reference = build(signal, false);
        const TPeakLevels final = reference->levels();

        TPeakPyramidBuilder growing(48000, 2);
        std::atomic<bool> done{false};
        std::thread writer([&] {
            qint64 position = 0;
            std::mt19937_64 random(7);
            while (position < length) {
                const qint64 count = std::min<qint64>(std::uniform_int_distribution<qint64>(1, 4096)(random), length - position);
                const float* pointers[2] = {signal.channels[0].data() + position, signal.channels[1].data() + position};
                growing.append(pointers, count);
                position += count;
                std::this_thread::yield();
            }
            growing.finish();
            done.store(true);
        });

        int snapshots = 0;
        qint64 lastCount = 0;
        bool finished = false;
        while (!finished) {
            finished = done.load();   // one more snapshot after the writer is done
            const TPeakLevels snapshot = growing.levels();
            ++snapshots;
            if (snapshot.level_count() == 0) {
                continue;
            }
            CHECK(snapshot.block_count(0) >= lastCount, "level 0 shrank");
            lastCount = snapshot.block_count(0);
            for (int level = 0; level < snapshot.level_count(); ++level) {
                CHECK(snapshot.block_count(level) <= final.block_count(level), "snapshot level %d has more blocks than the final pyramid", level);
                for (int i = 0; i < 200; ++i) {
                    const qint64 k = random_between(0, snapshot.block_count(level) - 1);
                    for (int c = 0; c < 2; ++c) {
                        const TPeakBlock& x = snapshot.block(level, c, k);
                        const TPeakBlock& y = final.block(level, c, k);
                        CHECK(x.min == y.min && x.max == y.max, "snapshot level %d block %lld differs", level, (long long)k);
                    }
                }
            }
            for (int i = 0; i < 50; ++i) {
                const qint64 first = random_between(0, snapshot.block_count(0) - 1);
                const qint64 last = random_between(first + 1, snapshot.block_count(0));
                const TPeakBlock x = snapshot.range(0, first, last);
                const TPeakBlock y = final.range(0, first, last);
                CHECK(x.min == y.min && x.max == y.max, "snapshot range %lld..%lld differs", (long long)first, (long long)last);
            }
        }
        writer.join();
        CHECK(same_levels(growing.levels(), final), "the grown pyramid differs from the one-shot one");
        std::printf("    growing pyramid of %lld frames: %d snapshots\n", (long long)length, snapshots);
    }
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
        const std::unique_ptr<TPeakPyramidBuilder> builder = build(signal, true);
        MemorySource memory(signal);

        for (int range = 0; range < 10; ++range) {
            const qint64 first = random_between(-100, frames + 100);
            const qint64 last = random_between(first - 10, frames + 200);
            float expected = 0.0f;
            for (qint64 n = std::max<qint64>(first, 0); n < std::min(last, frames); ++n) {
                for (int c = 0; c < channelCount; ++c) {
                    expected = std::max(expected, std::fabs(signal.channels[size_t(c)][size_t(n)]));
                }
            }
            memory.reads = 0;
            const float amplitude = TWaveformColumns::max_amplitude(builder->levels(), &memory, first, last);
            CHECK(amplitude == expected, "round %d range %lld..%lld: %f expected %f", round, (long long)first, (long long)last,
                  amplitude, expected);
            // Reads: the two partial blocks plus the candidate blocks
            const qint64 covered = std::min(last, frames) - std::max<qint64>(first, 0);
            if (covered > 4 * TPeakLevels::BlockFrames) {
                CHECK(memory.reads < covered, "round %d: read %lld of %lld frames", round, (long long)memory.reads, (long long)covered);
            }
        }
    }
}

int main()
{
    run("time conversions", test_time_conversions);
    run("pyramid", test_pyramid);
    run("file", test_file);
    run("columns", test_columns);
    run("max amplitude", test_max_amplitude);
    run("growing pyramid", test_growing_pyramid);
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
