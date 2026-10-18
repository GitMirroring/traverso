// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TPeakBuildService.h"

#include "TDiskIOThread.h"
#include "TPeakBuildTask.h"

#include <QPromise>

// A task of DiskIO's idle time that drives task and reports through a
// promise. A promise destroyed unfinished (DiskIO stops) cancels and
// finishes its future.
template <typename Task>
static QFuture<TPeakBuildResult> queue(std::unique_ptr<Task> task, TDiskIOThread* diskio)
{
    struct Job
    {
        std::unique_ptr<Task> task;
        QPromise<TPeakBuildResult> promise;
        bool started{false};
    };
    auto job = std::make_shared<Job>();
    job->task = std::move(task);
    QFuture<TPeakBuildResult> future = job->promise.future();

    diskio->add_idle_task([job](std::chrono::nanoseconds budget) {
        if (!job->started) {
            job->started = true;
            job->promise.start();
            job->promise.setProgressRange(0, 100);
        }
        if (job->promise.isCanceled()) {
            job->task->cancel();
        }
        const bool more = job->task->step(budget);
        job->promise.setProgressValue(job->task->progress());
        if (!more) {
            job->promise.addResult(TPeakBuildResult{job->task->state() == Task::State::Done, job->task->error()});
            job->promise.finish();
        }
        return more;
    });
    return future;
}

const QFuture<TPeakBuildResult>* TPeakBuildService::running(const QString& peakPath)
{
    // Forget the jobs that are over, the hash stays small that way. (Not
    // QFuture::isValid(): that tells whether results can be taken; it stays
    // false until the job started, reportStarted().)
    for (auto it = m_jobs.begin(); it != m_jobs.end();) {
        if (!it->isFinished()) {
            ++it;
            continue;
        }
        if (!it->isCanceled() && it->resultCount() > 0 && it->result().ok) {
            m_whole.insert(it.key());
        }
        it = m_jobs.erase(it);
    }
    const auto it = m_jobs.constFind(peakPath);
    return it != m_jobs.constEnd() ? &*it : nullptr;
}

QFuture<TPeakBuildResult> TPeakBuildService::build(const QString& audioPath, const QString& peakPath, TDiskIOThread* diskio)
{
    if (const QFuture<TPeakBuildResult>* job = running(peakPath)) {
        return *job;
    }
    Q_ASSERT(diskio);
    QFuture<TPeakBuildResult> job = queue(std::make_unique<TPeakBuildTask>(TPeakBuildTask::audio_file(audioPath), audioPath, peakPath),
                                          diskio);
    m_jobs.insert(peakPath, job);
    return job;
}

QFuture<TPeakBuildResult> TPeakBuildService::verify(const QString& peakPath, const TPeakFile::SourceInfo& expected,
                                                    TDiskIOThread* diskio)
{
    // A build or check of this file going on answers the question too
    if (const QFuture<TPeakBuildResult>* job = running(peakPath)) {
        return *job;
    }
    if (m_whole.contains(peakPath)) {
        return QtFuture::makeReadyValueFuture(TPeakBuildResult{true, {}});
    }
    Q_ASSERT(diskio);
    QFuture<TPeakBuildResult> job = queue(std::make_unique<TPeakVerifyTask>(peakPath, expected), diskio);
    m_jobs.insert(peakPath, job);
    return job;
}

void TPeakBuildService::cancel_all()
{
    for (QFuture<TPeakBuildResult>& job : m_jobs) {
        job.cancel();
    }
    m_jobs.clear();
    m_whole.clear();
}
