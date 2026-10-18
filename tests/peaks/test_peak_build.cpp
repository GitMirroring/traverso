// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of TPeakBuildTask: a build in slices writes the same peak
// file as the whole pyramid written in one go, whatever the slice sizes and
// budgets; sources that end early, fail, have too many channels or are
// cancelled leave no valid file behind.

#include "TPeakBuildTask.h"
#include "TPeakFile.h"
#include "TPeakPyramid.h"

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <random>
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

static TPeakBuildTask::State run(TPeakBuildTask& task, std::chrono::nanoseconds budget, int cancelAfterSteps = -1)
{
    int steps = 0;
    while (task.step(budget)) {
        if (++steps == cancelAfterSteps) {
            task.cancel();
        }
    }
    return task.state();
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

int main()
{
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
        CHECK(file.open(peakPath, expected) == TPeakFile::Status::Ok && file.levels().frame_count() == signal.frames()
              && file.verify_checksum(), "ends early: the file does not open for the reported length");
        file.close();
        CHECK(file_bytes(peakPath) == reference_file(referencePath, signal, audioPath, 3), "ends early: file differs");
    }

    // A finished recording's pyramid written in slices: the same file
    for (int round = 0; round < 10; ++round) {
        const int periodShift = int(random_between(0, 6));
        const Signal signal = make_signal(int(random_between(1, 2)), random_between(1, 200000));
        auto pyramid = std::make_shared<TPeakPyramidBuilder>(signal.rate, int(signal.channels.size()), periodShift);
        std::vector<const float*> pointers;
        for (const auto& channel : signal.channels) {
            pointers.push_back(channel.data());
        }
        pyramid->append(pointers.data(), signal.frames());
        pyramid->finish();
        const QByteArray reference = reference_file(referencePath, signal, audioPath, periodShift);
        TPeakWriteTask task(pyramid, audioPath, peakPath);
        int steps = 0;
        while (task.step(std::chrono::nanoseconds(0))) {
            ++steps;
        }
        CHECK(task.state() == TPeakWriteTask::State::Done, "write round %d: %s", round, qPrintable(task.error()));
        CHECK(file_bytes(peakPath) == reference, "write round %d: file differs from the one-go file", round);
        CHECK(steps >= std::min<qint64>(pyramid->complete_periods(), 2), "write round %d: %d steps for %lld periods, not sliced",
              round, steps, (long long)pyramid->complete_periods());
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
        CHECK(QFileInfo(part).size() >= 72 + 40 * (4 + 2 + 1) * 4 * 2, "%lld bytes: the periods were not streamed",
              (long long)QFileInfo(part).size());
        task.cancel();
        CHECK(run(task, std::chrono::nanoseconds(0)) == TPeakBuildTask::State::Cancelled, "cancel not honoured");
        CHECK(!QFile::exists(part) && !QFile::exists(peakPath), "a cancelled build left a file behind");
    }

    // Sources the task refuses
    {
        const Signal three = make_signal(3, 5000);
        TPeakBuildTask task(memory_source(three), audioPath, peakPath);
        CHECK(run(task, std::chrono::nanoseconds(0)) == TPeakBuildTask::State::Failed, "three channels accepted");
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
