// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TPeakBuildService.h"

#include "TDiskIOThread.h"
#include "TPeakBuildTask.h"

#include <QPromise>

// A build in DiskIO's idle time, one read per round. When it is over (or
// DiskIO goes first) DiskIO hands it back (retire()), and the rest happens on
// the GUI thread, where the service lives and which reads the stream: the
// file gets its name there (TPeakStream::publish() lets go of the GUI's
// mapping first, Windows renames no mapped file), then the future
// finishes, so a waveform that opens the file finds it. Posted to the
// service: when the project goes first, the call goes with it (the promise
// then cancels its future).
class TPeakBuildJob final : public TDiskIOTask
{
public:
    struct State
    {
        std::unique_ptr<TPeakBuildTask> task;
        QPromise<TPeakBuildResult> promise;
    };

    TPeakBuildJob(std::shared_ptr<State> state, QObject* service) : m_state(std::move(state)), m_service(service) {}

    void retire() override
    {
        QMetaObject::invokeMethod(m_service, [state = m_state] {
            TPeakBuildTask& task = *state->task;
            if (task.state() == TPeakBuildTask::State::Waiting || task.state() == TPeakBuildTask::State::Running) {
                // DiskIO went before the build ended (its sheet was deleted):
                // cancelled, the waveform asks again
                task.cancel();
                state->promise.future().cancel();
                state->promise.finish();
                return;
            }
            if (state->promise.isCanceled()) {
                task.cancel();
            }
            task.publish();
            state->promise.addResult(TPeakBuildResult{task.state() == TPeakBuildTask::State::Done, task.error()});
            state->promise.finish();
        }, Qt::QueuedConnection);
    }

    bool needs_work() const override { return !is_done(); }

    void step() override
    {
        if (m_state->promise.isCanceled()) {
            m_state->task->cancel();
        }
        m_state->task->step();
        m_state->promise.setProgressValue(m_state->task->progress());
    }

    bool is_done() const override
    {
        const TPeakBuildTask::State state = m_state->task->state();
        return state != TPeakBuildTask::State::Waiting && state != TPeakBuildTask::State::Running;
    }

private:
    std::shared_ptr<State> m_state;
    QObject* m_service;
};

TPeakBuild TPeakBuildService::build(const QString& audioPath, const QString& peakPath, uint rate, int channelCount,
                                    TDiskIOThread* diskio)
{
    // Forget the jobs that are over, the hash stays small that way
    for (auto it = m_jobs.begin(); it != m_jobs.end();) {
        it = it->future.isFinished() ? m_jobs.erase(it) : std::next(it);
    }
    const auto running = m_jobs.constFind(peakPath);
    if (running != m_jobs.constEnd()) {
        return TPeakBuild{running->future, running->stream.lock()};
    }
    Q_ASSERT(diskio);
    auto stream = std::make_shared<TPeakStream>(rate, channelCount, peakPath);
    auto state = std::make_shared<TPeakBuildJob::State>();
    state->task = std::make_unique<TPeakBuildTask>(TPeakBuildTask::audio_file(audioPath), audioPath, stream);
    // Started at once: older Qt versions finish the future of a destroyed
    // promise only when it was started (DiskIO can go before the first step)
    state->promise.start();
    state->promise.setProgressRange(0, 100);
    QFuture<TPeakBuildResult> job = state->promise.future();
    diskio->add_idle_task(std::make_unique<TPeakBuildJob>(std::move(state), this));
    m_jobs.insert(peakPath, Job{job, stream});
    return TPeakBuild{job, stream};
}

void TPeakBuildService::cancel_all()
{
    for (Job& job : m_jobs) {
        job.future.cancel();
    }
    m_jobs.clear();
}
