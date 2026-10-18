// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QFuture>
#include <QHash>
#include <QString>

#include <memory>

class TDiskIOThread;
class TPeakPyramidBuilder;

// The outcome of a peak build or write, for TWaveform
struct TPeakBuildResult
{
    enum class Kind
    {
        Build,      // from the audio file
        Write       // from a finished recording's pyramid
    };

    Kind kind{Kind::Build};
    bool ok{false};
    QString error;
};

/**
 * @brief Runs the peak builds and writes of a project in the idle time of a
 * sheet's DiskIO thread, one job per peak file.
 *
 * Asking for a peak file that is being built or written returns the running
 * job's future, so every waveform of the same source waits for one job. A
 * watcher of the future gets finished() exactly once, also for a job that
 * finished before it watched. Futures are cancelled only by cancel_all(),
 * when the project closes: a waveform that goes away does not cancel.
 * GUI thread only.
 *
 * Never wait for one of these futures (waitForFinished(), result() before
 * finished): the job runs in DiskIO's idle time, not in a thread pool.
 */
class TPeakBuildService
{
public:
    QFuture<TPeakBuildResult> build(const QString& audioPath, const QString& peakPath, TDiskIOThread* diskio);
    // pyramid must be finished and its audio file complete
    QFuture<TPeakBuildResult> write(std::shared_ptr<const TPeakPyramidBuilder> pyramid, const QString& audioPath,
                                    const QString& peakPath, TDiskIOThread* diskio);
    void cancel_all();

private:
    // The job of peakPath that is not over yet, or nullptr
    const QFuture<TPeakBuildResult>* running(const QString& peakPath);

    QHash<QString, QFuture<TPeakBuildResult>> m_jobs;
};
