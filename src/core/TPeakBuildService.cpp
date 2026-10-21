// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TPeakBuildService.h"

#include "TIOScheduler.h"
#include "TPeakCache.h"
#include "TPeakBuildTask.h"
#include "TPeakStream.h"

#include "TLog.h"

#include <QPromise>

// A build (or a recording's post-processing) in DiskIO's idle time, one
// step per round. The last step puts the file under its name. When the task
// is over (or DiskIO goes first) DiskIO hands it back (retire()), on its own
// thread or on the one that deletes it: the future finishes then, and a
// waveform that opens the file finds it.
class TPeakBuildJob final : public TIOTask
{
public:
    TPeakBuildJob(std::unique_ptr<TPeakBuildTask> task, QPromise<TPeakBuildResult> promise)
        : m_task(std::move(task)), m_promise(std::move(promise)) {}

    void retire() override
    {
        const TPeakBuildTask::State state = m_task->state();
        if (state == TPeakBuildTask::State::Waiting || state == TPeakBuildTask::State::Running) {
            // DiskIO went before the build ended (its sheet was deleted):
            // cancelled, the waveform asks again
            m_task->cancel();
            m_promise.future().cancel();
            m_promise.finish();
            return;
        }
        if (state == TPeakBuildTask::State::Done) {
            // A new file in the cache: used now, and the cache within its limit
            TPeakCache::touch(m_task->peak_path());
            TPeakCache::evict_later();
        }
        m_promise.addResult(TPeakBuildResult{state == TPeakBuildTask::State::Done, m_task->error()});
        m_promise.finish();
    }

    bool step() override
    {
        if (m_promise.isCanceled()) {
            m_task->cancel();
        }
        const bool more = m_task->step();
        m_promise.setProgressValue(m_task->progress());
        return more;
    }

private:
    std::unique_ptr<TPeakBuildTask> m_task;
    QPromise<TPeakBuildResult> m_promise;
};

TPeakBuild TPeakBuildService::add_job(std::unique_ptr<TPeakBuildTask> task, TIOScheduler* scheduler)
{
    Q_ASSERT(scheduler);
    const std::shared_ptr<TPeakStream> stream = task->stream();
    QPromise<TPeakBuildResult> promise;
    // Started at once: older Qt versions finish the future of a destroyed
    // promise only when it was started (DiskIO can go before the first step)
    promise.start();
    promise.setProgressRange(0, 100);
    QFuture<TPeakBuildResult> job = promise.future();
    scheduler->add_idle_task(std::make_unique<TPeakBuildJob>(std::move(task), std::move(promise)));
    m_jobs.insert(stream->peak_path(), Job{job, stream});
    return TPeakBuild{job, stream};
}

void TPeakBuildService::forget_finished()
{
    // The hash stays small that way
    for (auto it = m_jobs.begin(); it != m_jobs.end();) {
        it = it->future.isFinished() ? m_jobs.erase(it) : std::next(it);
    }
}

TPeakBuild TPeakBuildService::build(const QString& audioPath, const QString& peakPath, uint rate, int channelCount,
                                    TIOScheduler* scheduler)
{
    forget_finished();
    const auto running = m_jobs.constFind(peakPath);
    if (running != m_jobs.constEnd()) {
        TLOG_I("peaks", "waiting for the running job of {}", peakPath.toStdString());
        return TPeakBuild{running->future, running->stream.lock()};
    }
    TLOG_I("peaks", "building {}", peakPath.toStdString());
    auto stream = std::make_shared<TPeakStream>(rate, channelCount, peakPath);
    return add_job(std::make_unique<TPeakBuildTask>(TPeakBuildTask::audio_file(audioPath), audioPath, stream), scheduler);
}

void TPeakBuildService::post_process(std::shared_ptr<TPeakStream> recorded, TIOScheduler* scheduler)
{
    forget_finished();
    TLOG_I("peaks", "post-processing {}", recorded->peak_path().toStdString());
    add_job(std::make_unique<TPeakBuildTask>(std::move(recorded)), scheduler);
}

void TPeakBuildService::cancel_all()
{
    for (Job& job : m_jobs) {
        job.future.cancel();
    }
    m_jobs.clear();
}
