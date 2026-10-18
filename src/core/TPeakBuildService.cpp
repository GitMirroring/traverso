// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TPeakBuildService.h"

#include "TDiskIOThread.h"
#include "TPeakBuildTask.h"

#include <QPromise>
#include <QThreadPool>

// A build driven in DiskIO's idle time that reports through a promise. A
// promise destroyed unfinished (DiskIO stops) cancels and finishes its
// future.
static QFuture<TPeakBuildResult> queue(std::unique_ptr<TPeakBuildTask> task, TDiskIOThread* diskio)
{
    struct Job
    {
        std::unique_ptr<TPeakBuildTask> task;
        QPromise<TPeakBuildResult> promise;

        void report()
        {
            promise.addResult(TPeakBuildResult{task->state() == TPeakBuildTask::State::Done, task->error()});
            promise.finish();
        }
    };
    auto job = std::make_shared<Job>();
    job->task = std::move(task);
    // Started at once: older Qt versions finish the future of a destroyed
    // promise only when it was started (DiskIO stops before the first step)
    job->promise.start();
    job->promise.setProgressRange(0, 100);
    QFuture<TPeakBuildResult> future = job->promise.future();

    diskio->add_idle_task([job](std::chrono::nanoseconds budget) {
        if (job->promise.isCanceled()) {
            job->task->cancel();
        }
        if (job->task->step(budget)) {
            job->promise.setProgressValue(job->task->progress());
            return true;
        }
        if (job->task->state() == TPeakBuildTask::State::Written) {
            // Putting the file in place waits until it is on the disk, a
            // while on a slow disk: not on DiskIO, which serves the streams
            QThreadPool::globalInstance()->start([job] {
                if (job->promise.isCanceled()) {
                    job->task->cancel();
                }
                job->task->publish();
                job->report();
            });
        } else {
            job->report();
        }
        return false;
    });
    return future;
}

QFuture<TPeakBuildResult> TPeakBuildService::build(const QString& audioPath, const QString& peakPath, TDiskIOThread* diskio)
{
    // Forget the jobs that are over, the hash stays small that way
    for (auto it = m_jobs.begin(); it != m_jobs.end();) {
        it = it->isFinished() ? m_jobs.erase(it) : std::next(it);
    }
    const auto running = m_jobs.constFind(peakPath);
    if (running != m_jobs.constEnd()) {
        return *running;
    }
    Q_ASSERT(diskio);
    QFuture<TPeakBuildResult> job = queue(std::make_unique<TPeakBuildTask>(TPeakBuildTask::audio_file(audioPath), audioPath, peakPath),
                                          diskio);
    m_jobs.insert(peakPath, job);
    return job;
}

void TPeakBuildService::cancel_all()
{
    for (QFuture<TPeakBuildResult>& job : m_jobs) {
        job.cancel();
    }
    m_jobs.clear();
}
