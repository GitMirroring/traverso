// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of the chunked pyramid (TRAVPEAK version 4): the levels the
// stream makes while it is written are, record for record and byte for byte,
// the ones a separate pass over level 0 makes (the rules of version 3: a
// pair makes the block above it, a block without its pair moves up as it is,
// min and max combined, the RMS the root of the mean squares of the decoded
// level-0 codes, weighted by frames, rounded once). Lengths around the
// chunks' edges, one to six channels, NaN, inf and samples beyond +24 dB.

#include "TPeakFile.h"
#include "TPeakPyramid.h"
#include "TPeakStream.h"

#include <QFile>
#include <QTemporaryDir>

#include <cmath>
#include <cstdio>
#include <limits>
#include <random>
#include <vector>

static int failures = 0;

#define CHECK(condition, ...) \
    do { \
        if (!(condition)) { \
            std::printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            std::printf(__VA_ARGS__); \
            std::printf("\n"); \
            ++failures; \
        } \
    } while (false)

static std::mt19937_64 s_random(20261024);

static qint64 random_between(qint64 low, qint64 high)
{
    return std::uniform_int_distribution<qint64>(low, high)(s_random);
}

// A block of a level of the separate pass: min/max codes, mean squares, frames
struct OracleBlock
{
    std::vector<qint16> minMax;
    std::vector<double> squares;
    qint64 frames{0};
};

// Level 0 from the file, the levels above it pairwise, up to ChunkLevel
static std::vector<std::vector<OracleBlock>> oracle_levels(const TPeakLevels& levels)
{
    const int channels = levels.channel_count();
    std::vector<std::vector<OracleBlock>> result(1);
    for (qint64 k = 0; k < levels.block_count(0); ++k) {
        OracleBlock block;
        for (int c = 0; c < channels; ++c) {
            const TPeakBlock b = levels.block(0, c, k);
            block.minMax.push_back(b.min);
            block.minMax.push_back(b.max);
            const double rms = TPeakLevels::magnitude_value(std::max(int(levels.rms(0, c, k)), 0));
            block.squares.push_back(rms * rms);
        }
        block.frames = levels.block_frames(0, k);
        result[0].push_back(block);
    }
    for (int level = 1; level <= TPeakLevels::ChunkLevel; ++level) {
        const std::vector<OracleBlock>& below = result.back();
        std::vector<OracleBlock> above;
        for (size_t k = 0; k < below.size(); k += 2) {
            if (k + 1 == below.size()) {
                above.push_back(below[k]);
                continue;
            }
            const OracleBlock& a = below[k];
            const OracleBlock& b = below[k + 1];
            OracleBlock pair;
            pair.frames = a.frames + b.frames;
            for (int c = 0; c < channels; ++c) {
                pair.minMax.push_back(std::min(a.minMax[size_t(2 * c)], b.minMax[size_t(2 * c)]));
                pair.minMax.push_back(std::max(a.minMax[size_t(2 * c + 1)], b.minMax[size_t(2 * c + 1)]));
                pair.squares.push_back((a.squares[size_t(c)] * double(a.frames) + b.squares[size_t(c)] * double(b.frames))
                                       / double(pair.frames));
            }
            above.push_back(pair);
        }
        result.push_back(std::move(above));
    }
    return result;
}

static float sample(int channel)
{
    const qint64 kind = random_between(0, 4000);
    if (kind == 0) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    if (kind == 1) {
        return channel % 2 ? -std::numeric_limits<float>::infinity() : std::numeric_limits<float>::infinity();
    }
    if (kind == 2) {
        return float(random_between(-100, 100));   // beyond +24 dB now and then
    }
    // Quiet and loud stretches, so the RMS codes differ per level
    return float(std::uniform_real_distribution<double>(-1.0, 1.0)(s_random)) * (kind % 7 == 0 ? 0.001f : 0.5f);
}

int main()
{
    QTemporaryDir dir;
    if (!dir.isValid()) {
        std::printf("no temporary directory\n");
        return 1;
    }
    QFile audio(dir.filePath("source.wav"));
    if (!audio.open(QIODevice::WriteOnly) || audio.write("audio") != 5) {
        std::printf("cannot write the audio stand-in\n");
        return 1;
    }
    audio.close();

    constexpr qint64 Chunk = qint64(TPeakLevels::BlockFrames) << TPeakLevels::ChunkLevel;
    const std::vector<qint64> lengths = {1, 63, 64, 65, 129, Chunk - 64, Chunk - 1, Chunk, Chunk + 1, Chunk + 64, 2 * Chunk,
                                         2 * Chunk + 1, 3 * Chunk + 65, random_between(1, 5 * Chunk), random_between(1, 5 * Chunk)};
    int files = 0;
    qint64 compared = 0;
    for (const qint64 frames : lengths) {
        for (const int channels : {1, 2, 3, 6}) {
            std::vector<std::vector<float>> signal(static_cast<size_t>(channels), std::vector<float>(static_cast<size_t>(frames)));
            for (qint64 n = 0; n < frames; ++n) {
                for (int c = 0; c < channels; ++c) {
                    signal[size_t(c)][size_t(n)] = sample(c);
                }
            }

            // Written in pieces of any size, as a recording or a build writes
            const QString path = dir.filePath(QStringLiteral("%1.peaks").arg(++files));
            TPeakStream stream(48000, channels, path);
            CHECK(stream.open() == TPeakFile::Status::Ok, "open");
            for (qint64 position = 0; position < frames;) {
                const qint64 count = std::min(random_between(1, 5000), frames - position);
                std::vector<const float*> pointers;
                for (const auto& channel : signal) {
                    pointers.push_back(channel.data() + position);
                }
                stream.append(pointers.data(), count);
                position += count;
            }
            CHECK(stream.finish(audio.fileName()) == TPeakFile::Status::Ok, "finish");
            while (stream.post_process()) {
            }
            CHECK(stream.published(), "publish: %s", qPrintable(stream.error()));
            TPeakFile file;
            CHECK(file.open(path, TPeakFile::SourceInfo::from_file(audio.fileName(), 48000, channels, frames)) == TPeakFile::Status::Ok,
                  "%lld frames, %d channels: the file does not open", (long long)frames, channels);
            if (!file.is_open()) {
                continue;
            }
            const TPeakLevels& levels = file.levels();
            const std::vector<std::vector<OracleBlock>> expected = oracle_levels(levels);
            CHECK(levels.level_count() == TPeakLevels::ChunkLevel + 1, "%lld frames: %d levels", (long long)frames, levels.level_count());

            bool same = true;
            for (int level = 1; level < levels.level_count() && same; ++level) {
                const std::vector<OracleBlock>& blocks = expected[size_t(level)];
                CHECK(levels.block_count(level) == qint64(blocks.size()), "%lld frames level %d: %lld blocks, expected %zu",
                      (long long)frames, level, (long long)levels.block_count(level), blocks.size());
                for (qint64 k = 0; k < qint64(blocks.size()) && k < levels.block_count(level) && same; ++k) {
                    for (int c = 0; c < channels; ++c) {
                        const TPeakBlock block = levels.block(level, c, k);
                        const qint16 rms = TPeakLevels::code_nearest(float(std::sqrt(blocks[size_t(k)].squares[size_t(c)])));
                        if (block.min != blocks[size_t(k)].minMax[size_t(2 * c)] || block.max != blocks[size_t(k)].minMax[size_t(2 * c + 1)]
                                || levels.rms(level, c, k) != rms) {
                            CHECK(false, "%lld frames, %d channels: level %d block %lld channel %d is %d..%d rms %d, expected %d..%d rms %d",
                                  (long long)frames, channels, level, (long long)k, c, block.min, block.max, levels.rms(level, c, k),
                                  blocks[size_t(k)].minMax[size_t(2 * c)], blocks[size_t(k)].minMax[size_t(2 * c + 1)], rms);
                            same = false;
                            break;
                        }
                        ++compared;
                    }
                }
            }
        }
    }

    std::printf("%lld records compared\n", (long long)compared);
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all chunked pyramid tests passed\n");
    return 0;
}
