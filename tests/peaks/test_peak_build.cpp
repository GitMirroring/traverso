// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of TPeakBuildTask: a build in slices writes the same peak
// file as the whole pyramid written in one go, whatever the slice sizes and
// budgets; sources that end early, fail, have too many channels or are
// cancelled leave no valid file behind.

#include "TBufferedAudioStreamStatus.h"
#include "TPeakBuildTask.h"
#include "TPeakFile.h"
#include "TPeakPyramid.h"
#include "TPeakRecording.h"
#include "TWaveformColumns.h"

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <random>
#include <thread>
#include <vector>

static int s_failures = 0;

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

static std::mt19937_64 s_random(20261018);

static qint64 random_between(qint64 low, qint64 high)
{
    return std::uniform_int_distribution<qint64>(low, high)(s_random);
}

struct Signal
{
    uint rate{48000};
    std::vector<std::vector<float>> channels;
    qint64 frames() const { return channels.empty() ? 0 : qint64(channels.front().size()); }
};

static Signal make_signal(int channelCount, qint64 frames)
{
    Signal signal;
    std::uniform_real_distribution<float> noise(-1.2f, 1.2f);
    for (int c = 0; c < channelCount; ++c) {
        std::vector<float> samples(static_cast<size_t>(frames));
        for (qint64 n = 0; n < frames; ++n) {
            samples[size_t(n)] = float(std::sin(double(n) * 0.001 * (c + 1))) * 0.5f + noise(s_random) * 0.3f;
        }
        signal.channels.push_back(std::move(samples));
    }
    return signal;
}

// A source in memory that can report more frames than it has, fail at a
// frame, and deliver less than asked per read
class MemoryBuildSource final : public TPeakBuildSource
{
public:
    MemoryBuildSource(const Signal& signal, qint64 extraReported, qint64 failAt)
        : m_signal(signal), m_extraReported(extraReported), m_failAt(failAt) {}

    uint rate() const override { return m_signal.rate; }
    int channel_count() const override { return int(m_signal.channels.size()); }
    qint64 frame_count() const override { return m_signal.frames() + m_extraReported; }

    qint64 read(qint64 firstFrame, qint64 count, float* const* channels) override
    {
        if (m_failAt >= 0 && firstFrame + count > m_failAt) {
            return -1;
        }
        // Sometimes less than asked, like a decoder at a frame boundary
        const qint64 wanted = random_between(0, 3) == 0 ? random_between(1, count) : count;
        const qint64 frames = std::max<qint64>(0, std::min(wanted, m_signal.frames() - firstFrame));
        for (size_t c = 0; c < m_signal.channels.size(); ++c) {
            std::copy_n(m_signal.channels[c].data() + firstFrame, frames, channels[c]);
        }
        return frames;
    }

private:
    const Signal& m_signal;
    qint64 m_extraReported;
    qint64 m_failAt;
};

static TPeakBuildTask::OpenSource memory_source(const Signal& signal, qint64 extraReported = 0, qint64 failAt = -1)
{
    return [&signal, extraReported, failAt](QString*) -> std::unique_ptr<TPeakBuildSource> {
        return std::make_unique<MemoryBuildSource>(signal, extraReported, failAt);
    };
}

// Steps the task to its end and puts the file in place, as the service does
// (the latter from its thread pool)
static TPeakBuildTask::State run(TPeakBuildTask& task, std::chrono::nanoseconds budget, int cancelAfterSteps = -1)
{
    int steps = 0;
    while (task.step(budget)) {
        if (++steps == cancelAfterSteps) {
            task.cancel();
        }
    }
    return task.publish();
}

static QByteArray file_bytes(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// The whole pyramid of signal, written in one go
static QByteArray reference_file(const QString& path, const Signal& signal, const QString& audioPath, int periodShift)
{
    TPeakPyramidBuilder builder(signal.rate, int(signal.channels.size()), periodShift);
    std::vector<const float*> pointers;
    for (const auto& channel : signal.channels) {
        pointers.push_back(channel.data());
    }
    builder.append(pointers.data(), signal.frames());
    builder.finish();
    const auto source = TPeakFile::SourceInfo::from_file(audioPath, signal.rate, int(signal.channels.size()), signal.frames());
    CHECK(TPeakFile::write(path, builder.levels(), source, nullptr, periodShift) == TPeakFile::Status::Ok, "reference write");
    return file_bytes(path);
}

// The samples of a signal for TWaveformColumns (max_amplitude's ends)
class SignalSource final : public TWaveformSampleSource
{
public:
    explicit SignalSource(const Signal& signal) : m_signal(signal) {}

    qint64 read(qint64 firstFrame, qint64 count, float* const* channels) override
    {
        const qint64 available = std::max<qint64>(0, std::min(count, m_signal.frames() - firstFrame));
        for (size_t c = 0; c < m_signal.channels.size(); ++c) {
            std::copy_n(m_signal.channels[c].data() + firstFrame, available, channels[c]);
        }
        return available;
    }

private:
    const Signal& m_signal;
};

// Stress test of the lock-free paths: one thread records in buffers of 256
// frames, the GUI thread meanwhile queries columns at changing zoom over what
// is recorded, with RMS and max_amplitude. Every column made of blocks the
// snapshot has equals the finished pyramid's, no block is a poisoned (freed)
// one, the counts never go back, and the finished file answers the same.
static void stress_recording(const QString& peakPath, const QString& audioPath)
{
    const int shift = 4;   // periods of 1024 frames
    const Signal signal = make_signal(2, 1024 * 400 + 333);
    TPeakPyramidBuilder whole(signal.rate, 2, shift);
    {
        const float* pointers[2] = {signal.channels[0].data(), signal.channels[1].data()};
        whole.append(pointers, signal.frames());
        whole.finish();
    }
    const TPeakLevels expected = whole.levels();
    const double step = TPeakCodeMath::octave_fraction(1);

    QFile::remove(peakPath);
    TPeakRecording recording(signal.rate, 2, peakPath, shift);
    CHECK(recording.open() == TPeakFile::Status::Ok, "stress: open");
    std::atomic<bool> done{false};
    std::thread writer([&] {
        std::mt19937_64 random(42);
        for (qint64 position = 0; position < signal.frames(); position += 256) {
            const float* pointers[2] = {signal.channels[0].data() + position, signal.channels[1].data() + position};
            recording.append(pointers, std::min<qint64>(256, signal.frames() - position));
            if (std::uniform_int_distribution<int>(0, 30)(random) == 0) {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }
        CHECK(recording.finish(audioPath) == TPeakFile::Status::Ok, "stress: finish");
        done.store(true);
    });

    const qint64 widths[] = {64, 64 * 3, 64 * 16, 64 * 100 + 7, 64 * 1000};
    std::vector<TWaveformColumn> got(400);
    std::vector<TWaveformColumn> want(400);
    qint64 lastFrames = 0;
    qint64 lastBlocks = 0;
    int reads = 0;
    int compared = 0;
    bool failed = false;
    SignalSource source(signal);
    while ((!done.load() || reads == 0) && !failed) {
        recording.read([&](const TPeakLevels& view) {
            ++reads;
            const qint64 blocks = view.level_count() > 0 ? view.block_count(0) : 0;
            // The frames cover every block named (after finish() the last one
            // may be partial)
            if (view.frame_count() < lastFrames || blocks < lastBlocks
                    || (blocks > 0 && view.frame_count() <= (blocks - 1) * TPeakLevels::BlockFrames)) {
                CHECK(false, "stress read %d: %lld frames %lld blocks after %lld / %lld", reads, (long long)view.frame_count(),
                      (long long)blocks, (long long)lastFrames, (long long)lastBlocks);
                failed = true;
                return;
            }
            lastFrames = view.frame_count();
            lastBlocks = blocks;
            if (blocks == 0) {
                return;
            }
            // A freed block (debug builds poison them) has min > max
            for (int i = 0; i < 8; ++i) {
                const qint64 k = random_between(0, blocks - 1);
                for (int c = 0; c < 2; ++c) {
                    if (view.block(0, c, k).min > view.block(0, c, k).max) {
                        CHECK(false, "stress read %d: block %lld is poisoned", reads, (long long)k);
                        failed = true;
                        return;
                    }
                }
            }

            const qint64 frames = widths[random_between(0, 4)];
            const TTimeRef width(uint64_t(frames), signal.rate);
            const int count = int(got.size()) / 2;
            const TTimeRef start(uint64_t(random_between(0, blocks * TPeakLevels::BlockFrames - 1)), signal.rate);
            TWaveformColumns::query(view, nullptr, start, width, count, got.data());
            TWaveformColumns::query(expected, nullptr, start, width, count, want.data());
            for (int c = 0; c < 2; ++c) {
                for (int i = 0; i < count; ++i) {
                    // Only columns the snapshot has all blocks of
                    if (TTimeRef::frame_at_or_after(start + width * double(i + 1), signal.rate) > blocks * TPeakLevels::BlockFrames) {
                        break;
                    }
                    const TWaveformColumn& a = got[size_t(c * count + i)];
                    const TWaveformColumn& b = want[size_t(c * count + i)];
                    if (a.is_empty() != b.is_empty()
                            || (!a.is_empty() && (a.min != b.min || a.max != b.max
                                                  || a.rms > b.rms * step * 1.000001 || a.rms < b.rms / (step * 1.000001)))) {
                        CHECK(false, "stress read %d: column %d of %lld frames: %g..%g rms %g, expected %g..%g rms %g", reads, i,
                              (long long)frames, a.min, a.max, a.rms, b.min, b.max, b.rms);
                        failed = true;
                        return;
                    }
                    ++compared;
                }
            }

            // Now and then max_amplitude over a stretch of what is recorded
            if (reads % 16 == 0) {
                const qint64 first = random_between(0, blocks * TPeakLevels::BlockFrames - 1);
                const qint64 last = std::min(first + random_between(1, 30000), blocks * TPeakLevels::BlockFrames);
                float real = 0.0f;
                for (qint64 n = first; n < last; ++n) {
                    real = std::max({real, std::fabs(signal.channels[0][size_t(n)]), std::fabs(signal.channels[1][size_t(n)])});
                }
                const float amplitude = TWaveformColumns::max_amplitude(view, &source, first, last);
                if (amplitude < real || amplitude > real * float(step) * 1.000001f) {
                    CHECK(false, "stress read %d: max_amplitude %g, real %g", reads, amplitude, real);
                    failed = true;
                }
            }
        });
    }
    writer.join();
    CHECK(compared > 1000, "stress: only %d columns compared in %d reads", compared, reads);
    std::printf("    stress: %d reads, %d columns compared\n", reads, compared);

    // The finished file answers like the finished pyramid
    TPeakFile file;
    CHECK(file.open(peakPath, TPeakFile::SourceInfo::from_file(audioPath, signal.rate, 2, signal.frames())) == TPeakFile::Status::Ok,
          "stress: open the file");
    for (const qint64 frames : widths) {
        const TTimeRef width(uint64_t(frames), signal.rate);
        const int count = int(got.size()) / 2;
        TWaveformColumns::query(file.levels(), nullptr, TTimeRef(), width, count, got.data());
        TWaveformColumns::query(expected, nullptr, TTimeRef(), width, count, want.data());
        bool same = true;
        for (int i = 0; i < 2 * count; ++i) {
            same = same && got[size_t(i)].min == want[size_t(i)].min && got[size_t(i)].max == want[size_t(i)].max
                   && got[size_t(i)].rms == want[size_t(i)].rms;
        }
        CHECK(same, "stress: the file's columns of %lld frames differ", (long long)frames);
    }
}

int main()
{
    // When DiskIO may do idle work (builds): no stream needs reading. A stream
    // far from the transport (out of its window) needs nothing, also though it
    // is not in sync for the audio thread
    {
        TBufferedAudioStreamStatus status;
        status.set_sync_status(TBufferedAudioStreamStatus::OUT_OF_WINDOW);
        status.set_fill_status(0);
        CHECK(!status.needs_disk_work() && status.out_of_sync(), "a stream out of its window keeps DiskIO busy");
        status.set_sync_status(TBufferedAudioStreamStatus::OUT_OF_SYNC);
        status.set_fill_status(100);
        CHECK(status.needs_disk_work(), "a stream out of sync near the transport needs no reading");
        status.set_sync_status(TBufferedAudioStreamStatus::IN_SYNC);
        status.set_fill_status(80);
        CHECK(status.needs_disk_work(), "a stream with buffers at 80 %% needs no reading");
        status.set_fill_status(81);
        CHECK(!status.needs_disk_work(), "a stream with full buffers needs reading");
        status.set_sync_status(TBufferedAudioStreamStatus::QUEUE_SEEKING_TO_NEW_LOCATION);
        CHECK(status.needs_disk_work(), "a seeking stream needs no reading");
    }

    QTemporaryDir dir;
    CHECK(dir.isValid(), "no temporary directory");
    // Any file stands in for the audio file: its size and time go into the header
    const QString audioPath = dir.filePath("source.wav");
    {
        QFile audio(audioPath);
        CHECK(audio.open(QIODevice::WriteOnly) && audio.write("audio") == 5, "audio stand-in");
    }
    const QString referencePath = dir.filePath("reference.peaks");
    const QString peakPath = dir.filePath("sub/dir/built.peaks");   // the task creates the directory

    // Slices, budgets and period shifts do not change the file
    for (int round = 0; round < 40; ++round) {
        const int periodShift = int(random_between(0, 14));
        const qint64 period = qint64(TPeakLevels::BlockFrames) << periodShift;
        const qint64 frames = random_between(0, 1) ? random_between(1, 300000) : period * random_between(1, 4) + random_between(-2, 2);
        if (frames <= 0) {
            continue;
        }
        const Signal signal = make_signal(int(random_between(1, 2)), frames);
        const QByteArray reference = reference_file(referencePath, signal, audioPath, periodShift);

        TPeakBuildTask task(memory_source(signal), audioPath, peakPath, periodShift, random_between(64, 40000));
        const auto budget = random_between(0, 1) ? std::chrono::nanoseconds(0) : std::chrono::nanoseconds(std::chrono::milliseconds(50));
        CHECK(run(task, budget) == TPeakBuildTask::State::Done, "round %d: %s", round, qPrintable(task.error()));
        CHECK(task.progress() == 100, "round %d: progress %d", round, task.progress());
        CHECK(file_bytes(peakPath) == reference, "round %d shift %d frames %lld: file differs from the one-go file",
              round, periodShift, (long long)frames);
    }

    // A source that ends before the length it reports (MP3): the file holds
    // what was read and opens for the reported length
    {
        const Signal signal = make_signal(2, 100000);
        TPeakBuildTask task(memory_source(signal, 1152), audioPath, peakPath, 3);
        CHECK(run(task, std::chrono::nanoseconds(0)) == TPeakBuildTask::State::Done, "ends early: %s", qPrintable(task.error()));
        TPeakFile file;
        const auto expected = TPeakFile::SourceInfo::from_file(audioPath, signal.rate, 2, signal.frames() + 1152);
        CHECK(file.open(peakPath, expected) == TPeakFile::Status::Ok && file.levels().frame_count() == signal.frames(),
              "ends early: the file does not open for the reported length");
        file.close();
        CHECK(file_bytes(peakPath) == reference_file(referencePath, signal, audioPath, 3), "ends early: file differs");
    }

    // A recording: the writer thread streams periods to the part file while
    // the GUI thread reads snapshots from the mapping and memory; every block
    // a snapshot names is right, and the finished file is the one-go file
    for (int round = 0; round < 6; ++round) {
        const int periodShift = int(random_between(0, 4));
        const Signal signal = make_signal(int(random_between(1, 2)), random_between(200000, 600000));
        const QByteArray reference = reference_file(referencePath, signal, audioPath, periodShift);
        TPeakPyramidBuilder whole(signal.rate, int(signal.channels.size()), periodShift);
        {
            std::vector<const float*> pointers;
            for (const auto& channel : signal.channels) {
                pointers.push_back(channel.data());
            }
            whole.append(pointers.data(), signal.frames());
            whole.finish();
        }
        const TPeakLevels expected = whole.levels();

        QFile::remove(peakPath);
        TPeakRecording recording(signal.rate, int(signal.channels.size()), peakPath, periodShift);
        CHECK(recording.open() == TPeakFile::Status::Ok, "recording %d: open", round);
        std::atomic<bool> done{false};
        TPeakFile::Status finished = TPeakFile::Status::Error;
        std::thread writer([&] {
            std::mt19937_64 random(round);
            qint64 position = 0;
            while (position < signal.frames()) {
                const qint64 count = std::min<qint64>(std::uniform_int_distribution<qint64>(1, 3000)(random), signal.frames() - position);
                std::vector<const float*> pointers;
                for (const auto& channel : signal.channels) {
                    pointers.push_back(channel.data() + position);
                }
                recording.append(pointers.data(), count);
                position += count;
                if (std::uniform_int_distribution<int>(0, 50)(random) == 0) {
                    std::this_thread::sleep_for(std::chrono::microseconds(200));
                }
            }
            finished = recording.finish(audioPath);
            done.store(true);
        });

        int snapshots = 0;
        bool wrong = false;
        while (!done.load() || snapshots == 0) {
          recording.read([&](const TPeakLevels& view) {
            ++snapshots;
            for (int level = 0; level < view.level_count() && !wrong; ++level) {
                // Blocks of a growing pyramid equal the finished one's, except a
                // last block that finish() may still promote
                const qint64 blocks = std::min(view.block_count(level), expected.block_count(level) - 1);
                for (int c = 0; c < view.channel_count() && !wrong; ++c) {
                    for (qint64 k = 0; k < blocks; k += 1 + k / 7) {
                        const TPeakBlock& a = view.block(level, c, k);
                        const TPeakBlock& b = expected.block(level, c, k);
                        if (a.min != b.min || a.max != b.max || view.rms(level, c, k) != expected.rms(level, c, k)) {
                            wrong = true;
                            CHECK(false, "recording %d: level %d channel %d block %lld differs in snapshot %d", round, level, c,
                                  (long long)k, snapshots);
                            break;
                        }
                    }
                }
            }
          });
        }
        writer.join();
        CHECK(finished == TPeakFile::Status::Ok, "recording %d: finish", round);
        CHECK(file_bytes(peakPath) == reference, "recording %d: the streamed file differs from the one-go file", round);
        CHECK(!QFile::exists(TPeakFileWriter::part_path(peakPath)), "recording %d: part file left", round);
        // After finish the snapshot is the whole pyramid, records from the renamed file
        recording.read([&](const TPeakLevels& last) {
            CHECK(last.level_count() == expected.level_count() && last.frame_count() == expected.frame_count(), "recording %d: final view", round);
            for (int level = 0; level < last.level_count(); ++level) {
                for (qint64 k = 0; k < last.block_count(level); ++k) {
                    if (last.block(level, 0, k).min != expected.block(level, 0, k).min) {
                        CHECK(false, "recording %d: final view level %d block %lld", round, level, (long long)k);
                        break;
                    }
                }
            }
        });
        std::printf("    recording of %lld frames: %d snapshots\n", (long long)signal.frames(), snapshots);
    }

    stress_recording(peakPath, audioPath);

    // Recording memory without and with readers: helpers
    auto record_into = [&](TPeakRecording& recording, const Signal& signal, qint64 from, qint64 to) {
        for (qint64 position = from; position < to;) {
            const qint64 count = std::min<qint64>(1024, to - position);
            std::vector<const float*> pointers;
            for (const auto& channel : signal.channels) {
                pointers.push_back(channel.data() + position);
            }
            recording.append(pointers.data(), count);
            position += count;
        }
    };
    auto same_as = [&](const TPeakLevels& view, const TPeakLevels& expected) {
        for (int level = 0; level < view.level_count(); ++level) {
            const qint64 blocks = std::min(view.block_count(level), expected.block_count(level) - 1);
            for (int c = 0; c < view.channel_count(); ++c) {
                for (qint64 k = 0; k < blocks; ++k) {
                    if (view.block(level, c, k).min != expected.block(level, c, k).min
                            || view.block(level, c, k).max != expected.block(level, c, k).max
                            || view.rms(level, c, k) != expected.rms(level, c, k)) {
                        return false;
                    }
                }
            }
        }
        return true;
    };
    auto whole_pyramid = [&](const Signal& signal, int shift) {
        auto builder = std::make_unique<TPeakPyramidBuilder>(signal.rate, int(signal.channels.size()), shift);
        std::vector<const float*> pointers;
        for (const auto& channel : signal.channels) {
            pointers.push_back(channel.data());
        }
        builder->append(pointers.data(), signal.frames());
        builder->finish();
        return builder;
    };

    // Nothing reads: the written periods leave memory anyway. The memory does
    // not grow with the length: after 36 periods about what it was after 12
    // (the current period plus one chunk of 1024 blocks per level; a level
    // more per doubling of the length)
    {
        const int shift = 11;   // periods of 2048 level-0 blocks
        const qint64 period = qint64(TPeakLevels::BlockFrames) << shift;
        const Signal signal = make_signal(2, period * 36 + period / 2);
        QFile::remove(peakPath);
        TPeakRecording recording(signal.rate, 2, peakPath, shift);
        CHECK(recording.open() == TPeakFile::Status::Ok, "no reader: open");
        record_into(recording, signal, 0, period * 12 + period / 2);
        const qint64 afterTwelve = recording.memory_blocks();
        record_into(recording, signal, period * 12 + period / 2, signal.frames());
        const qint64 afterThirtySix = recording.memory_blocks();
        const qint64 all = 2 * 2 * ((signal.frames() + 63) / 64);   // every block of every level, both channels
        CHECK(afterThirtySix <= afterTwelve + 2 * 2 * 1024 && afterThirtySix < all / 8,
              "no reader: %lld blocks in memory after 36 periods, %lld after 12 (all: %lld)",
              (long long)afterThirtySix, (long long)afterTwelve, (long long)all);
        CHECK(recording.finish(audioPath) == TPeakFile::Status::Ok, "no reader: finish");
        CHECK(file_bytes(peakPath) == reference_file(referencePath, signal, audioPath, shift), "no reader: file differs");
    }

    // A reader that pauses inside its read while the writer passes periods:
    // its snapshot stays right, the memory is freed after the read
    {
        const int shift = 11;
        const Signal signal = make_signal(1, qint64(TPeakLevels::BlockFrames) * (2048 * 10 + 50));
        const auto expected = whole_pyramid(signal, shift);
        const TPeakLevels expectedLevels = expected->levels();
        QFile::remove(peakPath);
        TPeakRecording recording(signal.rate, 1, peakPath, shift);
        CHECK(recording.open() == TPeakFile::Status::Ok, "paused reader: open");
        const qint64 third = (signal.frames() / 3) & ~qint64(63);
        record_into(recording, signal, 0, third);
        std::atomic<bool> reading{false};
        std::atomic<bool> release{false};
        std::thread writer([&] {
            while (!reading.load()) {
                std::this_thread::yield();
            }
            record_into(recording, signal, third, 2 * third);   // several periods while the reader holds its snapshot
            release.store(true);
        });
        bool right = false;
        qint64 heldMemory = 0;
        recording.read([&](const TPeakLevels& view) {
            reading.store(true);
            while (!release.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            heldMemory = recording.memory_blocks();
            right = same_as(view, expectedLevels);
        });
        writer.join();
        CHECK(right, "paused reader: its snapshot changed under it");
        record_into(recording, signal, 2 * third, signal.frames());
        CHECK(recording.memory_blocks() < heldMemory, "paused reader: memory not freed after the read (%lld, held %lld)",
              (long long)recording.memory_blocks(), (long long)heldMemory);
        CHECK(recording.finish(audioPath) == TPeakFile::Status::Ok, "paused reader: finish");
    }

    // The part file cannot be written: peaks stay in memory, no peak file
    {
        const Signal signal = make_signal(1, 300000);
        const auto expected = whole_pyramid(signal, 2);
        const QString blocked = dir.filePath("blocked");
        {
            QFile blocker(blocked);   // a file where the directory would be
            CHECK(blocker.open(QIODevice::WriteOnly), "write failure: blocker");
        }
        TPeakRecording recording(signal.rate, 1, blocked + "/x.peaks", 2);
        CHECK(recording.open() != TPeakFile::Status::Ok, "write failure: opened");
        record_into(recording, signal, 0, signal.frames());
        bool right = false;
        recording.read([&](const TPeakLevels& view) { right = same_as(view, expected->levels()) && view.block_count(0) > 0; });
        CHECK(right, "write failure: the peaks in memory are wrong");
        CHECK(recording.finish(audioPath) != TPeakFile::Status::Ok, "write failure: finish reports Ok");
    }

    // The mapping cannot follow (the part file went): the snapshot ends where
    // the mapping ends, and is right
    {
        const int shift = 2;
        const Signal signal = make_signal(1, 200000);
        const auto expected = whole_pyramid(signal, shift);
        QFile::remove(peakPath);
        TPeakRecording recording(signal.rate, 1, peakPath, shift);
        CHECK(recording.open() == TPeakFile::Status::Ok, "remap failure: open");
        record_into(recording, signal, 0, signal.frames() / 4);
        qint64 mappedBlocks = 0;
        recording.read([&](const TPeakLevels& view) { mappedBlocks = view.block_count(0); });
        QFile::remove(TPeakFileWriter::part_path(peakPath));   // the writer keeps writing to the unlinked file
        record_into(recording, signal, signal.frames() / 4, signal.frames() / 2);
        bool right = false;
        qint64 blocks = 0;
        recording.read([&](const TPeakLevels& view) { right = same_as(view, expected->levels()); blocks = view.block_count(0); });
        CHECK(right, "remap failure: wrong blocks");
        CHECK(blocks <= mappedBlocks + 4 && blocks < (signal.frames() / 2) / 64, "remap failure: %lld blocks, the mapping covers %lld",
              (long long)blocks, (long long)mappedBlocks);
        recording.finish(audioPath);
    }

    auto expect_no_file = [&](const Signal& signal, qint64 reported, const char* what) {
        TPeakFile file;
        const auto expected = TPeakFile::SourceInfo::from_file(audioPath, signal.rate, int(signal.channels.size()), reported);
        CHECK(file.open(peakPath, expected) != TPeakFile::Status::Ok, "%s: left a valid file", what);
    };

    // A read error in the middle
    {
        const Signal signal = make_signal(1, 200000);
        QFile::remove(peakPath);
        TPeakBuildTask task(memory_source(signal, 0, 150000), audioPath, peakPath, 2);
        CHECK(run(task, std::chrono::nanoseconds(0)) == TPeakBuildTask::State::Failed, "read error not reported");
        expect_no_file(signal, signal.frames(), "read error");
    }

    // Cancelled half way
    {
        const Signal signal = make_signal(2, 300000);
        QFile::remove(peakPath);
        TPeakBuildTask task(memory_source(signal), audioPath, peakPath, 2, 1024);
        for (int steps = 0; steps < 50; ++steps) {
            task.step(std::chrono::nanoseconds(0));
        }
        // Streamed: the periods completed so far are on disk, in the part
        // file (50 slices of at most 1024 frames, periods of 256 frames)
        const QString part = TPeakFileWriter::part_path(peakPath);
        CHECK(QFileInfo(part).size() >= TPeakFileWriter::records_end(2, 2, 40), "%lld bytes: the periods were not streamed",
              (long long)QFileInfo(part).size());
        task.cancel();
        CHECK(run(task, std::chrono::nanoseconds(0)) == TPeakBuildTask::State::Cancelled, "cancel not honoured");
        CHECK(!QFile::exists(part) && !QFile::exists(peakPath), "a cancelled build left a file behind");
    }

    // Written but not published yet: the file is not under its name; a
    // cancel in between leaves nothing behind
    {
        const Signal signal = make_signal(1, 100000);
        QFile::remove(peakPath);
        TPeakBuildTask task(memory_source(signal), audioPath, peakPath, 2);
        while (task.step(std::chrono::nanoseconds(0))) {
        }
        const QString part = TPeakFileWriter::part_path(peakPath);
        CHECK(task.state() == TPeakBuildTask::State::Written && QFile::exists(part) && !QFile::exists(peakPath),
              "a written build is not in its part file");
        task.cancel();
        CHECK(task.publish() == TPeakBuildTask::State::Cancelled && !QFile::exists(part) && !QFile::exists(peakPath),
              "a build cancelled before publishing left a file behind");
    }

    // Sources the task refuses
    {
        const Signal three = make_signal(3, 5000);
        TPeakBuildTask task(memory_source(three), audioPath, peakPath);
        CHECK(run(task, std::chrono::nanoseconds(0)) == TPeakBuildTask::State::Unsupported && !task.error().isEmpty(),
              "three channels accepted");
        CHECK(!TPeakBuildTask::supports(3) && TPeakBuildTask::supports(2) && TPeakBuildTask::supports(1), "supports()");
        const Signal empty = make_signal(1, 0);
        TPeakBuildTask none(memory_source(empty), audioPath, peakPath);
        CHECK(run(none, std::chrono::nanoseconds(0)) == TPeakBuildTask::State::Failed, "empty source accepted");
        TPeakBuildTask unreadable([](QString* error) -> std::unique_ptr<TPeakBuildSource> {
            *error = QStringLiteral("no decoder");
            return nullptr;
        }, audioPath, peakPath);
        CHECK(run(unreadable, std::chrono::nanoseconds(0)) == TPeakBuildTask::State::Failed && unreadable.error() == "no decoder",
              "unopenable source");
        TPeakBuildTask missing(TPeakBuildTask::audio_file(dir.filePath("none.wav")), audioPath, peakPath);
        CHECK(run(missing, std::chrono::nanoseconds(0)) == TPeakBuildTask::State::Failed, "missing audio file accepted");
    }

    if (s_failures) {
        std::printf("%d failures\n", s_failures);
        return 1;
    }
    std::printf("all peak build tests passed\n");
    return 0;
}
