// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// A measurement, not part of ctest (dispatcher-conclusie.md, acceptance):
// the peak builds of every audio file in a directory, as idle work of a
// TIOScheduler with a pool of its own. Prints the time until all are built
// and how much of the threads the steps used (the steps' time against
// threads × that time). Build it in Release.
//
// Usage: traverso_measure_idle_builds <directory> [threads] [idle limit]
// (default: half the cores, and as many idle steps as threads)

#include "ResampleAudioReader.h"
#include "TIOScheduler.h"
#include "TIOTask.h"
#include "TPeakBuildTask.h"
#include "TPeakStream.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QTemporaryDir>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>

// A build as the read scheduler runs it, timed per step
class MeasuredBuild final : public TIOTask
{
public:
    MeasuredBuild(std::unique_ptr<TPeakBuildTask> task, std::atomic<int>& done, std::atomic<int>& failed,
                  std::atomic<qint64>& stepNanoseconds)
        : m_task(std::move(task)), m_done(done), m_failed(failed), m_stepNanoseconds(stepNanoseconds) {}

    bool step() override
    {
        const auto start = std::chrono::steady_clock::now();
        const bool more = m_task->step();
        m_stepNanoseconds.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count());
        return more;
    }
    void retire() override
    {
        if (m_task->state() != TPeakBuildTask::State::Done) {
            m_failed.fetch_add(1);
        }
        m_done.fetch_add(1);
    }

private:
    std::unique_ptr<TPeakBuildTask> m_task;
    std::atomic<int>& m_done;
    std::atomic<int>& m_failed;
    std::atomic<qint64>& m_stepNanoseconds;
};

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 2) {
        std::printf("usage: %s <directory> [threads] [idle limit]\n", argv[0]);
        return 2;
    }
    const QDir directory(QString::fromLocal8Bit(argv[1]));
    const int threads = std::max(argc > 2 ? std::atoi(argv[2]) : QThread::idealThreadCount() / 2, 2);
    const int idleLimit = argc > 3 ? std::atoi(argv[3]) : threads;
    QTemporaryDir peaks;

    std::atomic<int> done{0};
    std::atomic<int> failed{0};
    std::atomic<qint64> stepNanoseconds{0};
    const QStringList files = directory.entryList(QDir::Files, QDir::Name);

    TIOScheduler scheduler(threads);
    scheduler.set_idle_limit(idleLimit);
    scheduler.start();

    QElapsedTimer timer;
    timer.start();
    for (const QString& name : files) {
        const QString path = directory.filePath(name);
        TResampleAudioReader reader(path);
        if (!reader.is_valid()) {
            std::printf("skipped: %s\n", qPrintable(name));
            done.fetch_add(1);
            continue;
        }
        auto stream = std::make_shared<TPeakStream>(reader.get_file_rate(), int(reader.get_num_channels()), peaks.filePath(name + ".peaks"));
        auto task = std::make_unique<TPeakBuildTask>(TPeakBuildTask::audio_file(path), path, stream);
        scheduler.add_idle_task(std::make_unique<MeasuredBuild>(std::move(task), done, failed, stepNanoseconds));
    }
    while (done.load() < int(files.size())) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const double seconds = double(timer.nsecsElapsed()) / 1e9;
    const double stepSeconds = double(stepNanoseconds.load()) / 1e9;

    std::printf("%d files, %d threads, idle limit %d: all built in %.3f s, steps %.3f s, %.0f%% of the threads used, %d failed\n",
                int(files.size()), threads, idleLimit, seconds, stepSeconds, 100.0 * stepSeconds / (threads * seconds), failed.load());
    return failed.load() ? 1 : 0;
}
