// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QFuture>
#include <QHash>
#include <QString>

#include <memory>

class TIOScheduler;
class TPeakBuildTask;
class TPeakStream;

// The outcome of a peak build, for TWaveform
struct TPeakBuildResult
{
    bool ok{false};
    QString error;
};

// A build, and the peaks it streams as it goes (read() them on the GUI
// thread to show the waveform growing)
struct TPeakBuild
{
    QFuture<TPeakBuildResult> future;
    std::shared_ptr<TPeakStream> stream;
};

/**
 * @brief Runs the peak builds of a project in the idle time of a sheet's
 * read DiskIO thread, one job per peak file, one step per DiskIO round, and
 * the post-processing of its recordings (a recording writes level 0 of its
 * peak file as it records: TPeakStream).
 *
 * Asking for a peak file that is being built, or post-processed, returns the
 * running job's future, so every waveform of the same source waits for one
 * job. A watcher of the future gets finished() exactly once, also for a job
 * that finished before it watched. Futures are cancelled only by
 * cancel_all(), when the project closes: a waveform that goes away does not
 * cancel. GUI thread only.
 *
 * Never wait for one of these futures (waitForFinished(), result() before
 * finished): the job runs in DiskIO's idle time, not in a thread pool.
 */
class TPeakBuildService
{
public:
    // rate, channelCount: the source's, as its waveform knows them
    TPeakBuild build(const QString& audioPath, const QString& peakPath, uint rate, int channelCount, TIOScheduler* scheduler);
    // A recording's peaks after TPeakStream::finish(): the levels above level
    // 0 and the file's name. Before the recording's file waveform asks for
    // its peak file, so that one joins this job.
    void post_process(std::shared_ptr<TPeakStream> recorded, TIOScheduler* scheduler);
    void cancel_all();

private:
    TPeakBuild add_job(std::unique_ptr<TPeakBuildTask> task, TIOScheduler* scheduler);
    void forget_finished();

    // The stream is not kept alive here: the job holds it while it runs,
    // the waveforms while they show it
    struct Job
    {
        QFuture<TPeakBuildResult> future;
        std::weak_ptr<TPeakStream> stream;
    };
    QHash<QString, Job> m_jobs;
};
