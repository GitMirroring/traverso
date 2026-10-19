// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QFuture>
#include <QHash>
#include <QString>

class TDiskIOThread;

// The outcome of a peak build, for TWaveform
struct TPeakBuildResult
{
    bool ok{false};
    QString error;
};

/**
 * @brief Runs the peak builds of a project in the idle time of a sheet's
 * DiskIO thread, one job per peak file. The finished file is put in place
 * from the global thread pool. (A recording writes its own peak file as it
 * records: TPeakStream.)
 *
 * Asking for a peak file that is being built returns the running job's
 * future, so every waveform of the same source waits for one job. A
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
    void cancel_all();

private:
    QHash<QString, QFuture<TPeakBuildResult>> m_jobs;
};
