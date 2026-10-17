// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QThread>

#include <atomic>
#include <memory>

class TBufferedAudioStreamReader;
class TPeakPyramidBuilder;

/**
 * @brief One build of a peak file: reads the source, builds the pyramid and
 * writes the file. Runs on the TPeakBuilder thread, signals on the thread it
 * was created on.
 */
class TPeakBuildJob : public QObject
{
    Q_OBJECT

public:
    // Takes ownership of source, a reader of its own (not the clip's)
    TPeakBuildJob(TBufferedAudioStreamReader* source, const QString& peakPath);
    // Writes a finished pyramid (a recording's) for the audio file audioPath
    TPeakBuildJob(std::shared_ptr<TPeakPyramidBuilder> pyramid, const QString& audioPath, const QString& peakPath);
    ~TPeakBuildJob();

    const QString& peak_path() const { return m_peakPath; }
    void cancel() { m_cancelled.store(true); }
    bool is_done() const { return m_done.load(); }
    bool succeeded() const { return m_succeeded; }
    const QString& error() const { return m_error; }

signals:
    void progress(int percent);
    void finished();

private:
    friend class TPeakBuilder;
    void run();

    void build();
    void write();

    TBufferedAudioStreamReader* m_source{nullptr};
    std::shared_ptr<TPeakPyramidBuilder> m_pyramid;
    QString m_audioPath;
    QString m_peakPath;
    std::atomic<bool> m_cancelled{false};
    std::atomic<bool> m_done{false};
    bool m_succeeded{false};
    QString m_error;
};

/**
 * @brief Builds peak files in the background, one at a time; requests for
 * the same file share one job.
 */
class TPeakBuilder : public QObject
{
    Q_OBJECT

public:
    // The running or queued job for peakPath, or a new one. sourceId is the
    // resources manager id of the source to read.
    std::shared_ptr<TPeakBuildJob> build(const QString& peakPath, qint64 sourceId);
    // Writes a finished pyramid to peakPath on the builder thread; a build()
    // for the same path meanwhile waits for it
    std::shared_ptr<TPeakBuildJob> write(const QString& peakPath, std::shared_ptr<TPeakPyramidBuilder> pyramid, const QString& audioPath);

private:
    std::shared_ptr<TPeakBuildJob> queue(const QString& peakPath, TPeakBuildJob* job);

private:
    TPeakBuilder();
    ~TPeakBuilder();
    friend TPeakBuilder& peak_builder();

    QThread m_thread;
    QObject* m_worker;   // lives in m_thread, runs the jobs
    QMutex m_mutex;
    QHash<QString, std::weak_ptr<TPeakBuildJob>> m_jobs;
};

TPeakBuilder& peak_builder();
