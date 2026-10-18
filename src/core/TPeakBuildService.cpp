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

const QFuture<TPeakBuildResult>* TPeakBuildService::running(QHash<QString, QFuture<TPeakBuildResult>>& jobs,
                                                           const QString& peakPath)
{
    // Forget the jobs that are over, the hash stays small that way
    for (auto it = jobs.begin(); it != jobs.end();) {
        if (!it->isFinished()) {
            ++it;
            continue;
        }
        if (!it->isCanceled() && it->resultCount() > 0 && it->result().ok) {
            m_whole.insert(it.key());
        }
        it = jobs.erase(it);
    }
    const auto it = jobs.constFind(peakPath);
    return it != jobs.constEnd() ? &*it : nullptr;
}

QFuture<TPeakBuildResult> TPeakBuildService::build(const QString& audioPath, const QString& peakPath, TDiskIOThread* diskio)
{
    // Only a build answers a build: a check of the same file may fail
    if (const QFuture<TPeakBuildResult>* job = running(m_builds, peakPath)) {
        return *job;
    }
    Q_ASSERT(diskio);
    QFuture<TPeakBuildResult> job = queue(std::make_unique<TPeakBuildTask>(TPeakBuildTask::audio_file(audioPath), audioPath, peakPath),
                                          diskio);
    m_whole.remove(peakPath);
    m_builds.insert(peakPath, job);
    return job;
}

QFuture<TPeakBuildResult> TPeakBuildService::verify(const QString& peakPath, const TPeakFile::SourceInfo& expected,
                                                    TDiskIOThread* diskio)
{
    // A build or check of this file going on answers the question too
    if (const QFuture<TPeakBuildResult>* job = running(m_builds, peakPath)) {
        return *job;
    }
    if (const QFuture<TPeakBuildResult>* job = running(m_checks, peakPath)) {
        return *job;
    }
    if (m_whole.contains(peakPath)) {
        return QtFuture::makeReadyValueFuture(TPeakBuildResult{true, {}});
    }
    Q_ASSERT(diskio);
    QFuture<TPeakBuildResult> job = queue(std::make_unique<TPeakVerifyTask>(peakPath, expected), diskio);
    m_checks.insert(peakPath, job);
    return job;
}

void TPeakBuildService::cancel_all()
{
    for (QFuture<TPeakBuildResult>& job : m_builds) {
        job.cancel();
    }
    for (QFuture<TPeakBuildResult>& job : m_checks) {
        job.cancel();
    }
    m_builds.clear();
    m_checks.clear();
    m_whole.clear();
}
