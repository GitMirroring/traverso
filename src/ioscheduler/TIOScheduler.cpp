/*
Copyright (C) 2006-2024 Remon Sijrier

This file is part of Traverso

Traverso is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA.

*/

#include "TIOScheduler.h"

#include "TIOStream.h"
#include "Debugger.h"
#include "TLog.h"

#include <QFile>
#include <QFileInfo>
#include <QtConcurrent>
#include <QThreadPool>

#include <algorithm>

#if defined (Q_OS_LINUX)
// idle_limit_for(): the block device of a directory
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#endif

TIOScheduler::TIOScheduler()
    : m_lastCpuReadTime(TTimeRef::get_nanoseconds_since_epoch())
{
}

TIOScheduler::~TIOScheduler()
{
    PENTERDES;

    // No new work, the scheduler thread wakes and ends (a round that runs ends first:
    // its steps use the streams and tasks), then the tasks that are left go
    // back to their owners
    m_stopRequested.store(true);
    if (m_thread) {
        m_wake.signal();
        m_thread->wait();
    }
    Q_ASSERT_X(m_streams.empty() && m_streamsToAdd.empty(), "TIOScheduler", "streams are still registered");
    for (const std::unique_ptr<TIOTask>& task : m_tasksToAdd) {
        task->retire();
    }
    for (const std::unique_ptr<TIOTask>& task : m_idleTasks) {
        task->retire();
    }
}

void TIOScheduler::start()
{
    Q_ASSERT(!m_thread);
    m_thread.reset(QThread::create([this] { run(); }));
    m_thread->start();
}

bool TIOScheduler::on_scheduler_thread() const
{
    return m_thread ? QThread::currentThread() == m_thread.get() : QThread::currentThread() == thread();
}

void TIOScheduler::run()
{
    TLog::name_thread("diskio");

    bool more = false;
    while (true) {
        // The wake: the audio thread's tick every cycle (also with the
        // transport stopped), or a request. After a round with idle work the
        // next one follows at once; the wakes that came meanwhile are taken.
        if (!more) {
            m_wake.wait();
        }
        while (m_wake.tryWait()) {
        }
        if (m_stopRequested.load()) {
            break;
        }

        const trav_time_t startTime = TTimeRef::get_nanoseconds_since_epoch();
        more = dispatch();
        m_doWorktTime.fetch_add(TTimeRef::get_nanoseconds_since_epoch() - startTime);
    }

    TLOG_I("diskio", "stopped");
}

bool TIOScheduler::dispatch()
{
    take_requests();
    return do_round();
}

void TIOScheduler::take_requests()
{
    Q_ASSERT_X(on_scheduler_thread(), "TIOScheduler::take_requests", "not on the scheduler thread");

    std::lock_guard locker(m_requestsMutex);
    for (TIOStream* stream : m_streamsToAdd) {
        Q_ASSERT(std::find(m_streams.begin(), m_streams.end(), stream) == m_streams.end());
        m_streams.push_back(stream);
    }
    m_streamsToAdd.clear();
    // The ring buffers go here, where the audio thread does not use them;
    // the owner deletes the stream once remove_stream() returns
    for (TIOStream* stream : m_streamsToRemove) {
        std::erase(m_streams, stream);
        stream->retire();
    }
    m_streamsToRemove.clear();
    for (std::unique_ptr<TIOTask>& task : m_tasksToAdd) {
        m_idleTasks.push_back(std::move(task));
    }
    m_tasksToAdd.clear();
    m_taken = m_requested;
    m_requestsTaken.notify_all();
}

bool TIOScheduler::do_round()
{
    Q_ASSERT_X(on_scheduler_thread(), "TIOScheduler::do_round", "not on the scheduler thread");

    if (m_qualityChanged.exchange(false)) {
        for (TIOStream* stream : m_streams) {
            stream->set_output_rate_and_convertor_type(int(get_output_rate()), get_resample_quality());
        }
    }

    // A new rate is taken by the next seek (the owner seeks after it)
    // A seek is taken at the start of a round; the location is written
    // before the flag
    const bool seek = m_seekRequested.exchange(false, std::memory_order_acquire);
    m_round.seek = seek;
    m_round.seekLocation = TTimeRef(m_seekLocation.load(std::memory_order_relaxed));
    m_round.transportLocation = TTimeRef(m_transportLocation.load(std::memory_order_relaxed));
    m_round.sampleRateChanged = seek && m_rateChanged.exchange(false);
    m_round.outputRate = get_output_rate();
    m_round.resampleQuality = get_resample_quality();
    m_round.bufferSize = seek ? m_bufferSize.load(std::memory_order_relaxed) : 0;
    if (seek) {
        TLOG_D("diskio", "seek requested, seeking");
    }

    // What every stream needs; an idle task has work until its step says
    // it is over
    std::vector<TIOPlanner::Stream> streams;
    streams.reserve(m_streams.size());
    for (TIOStream* stream : m_streams) {
        streams.push_back(stream->disk_state(m_round.transportLocation));
    }

    // blockingMap() starts the round on the pool and waits: the pool's
    // threads do the work, this thread takes part only when the pool is busy
    // (it runs the round then alone, so the scheduler always gets on)
    const TIOPlanner::Round plan = m_planner.plan(streams, seek, int(m_idleTasks.size()),
                                                      m_idleWorkAllowed.load(std::memory_order_relaxed),
                                                      QThreadPool::globalInstance()->maxThreadCount(),
                                                      m_idleLimit.load(std::memory_order_relaxed));

    // A step of a stream (fill: what it had when the round looked at it,
    // afterwards what the GUI is told, -1 for nothing) or of an idle task.
    // Idle steps that have not started yet give way to a seek that comes in
    // meanwhile (they get their step in a later round); a read that runs
    // already finishes first.
    struct Step
    {
        TIOStream* stream{nullptr};
        TIOTask* idle{nullptr};
        int fill{-1};
        bool over{false};
    };
    std::vector<Step> round;
    round.reserve(plan.streams.size() + size_t(plan.idleSteps));
    for (const int index : plan.streams) {
        round.push_back(Step{m_streams[size_t(index)], nullptr, streams[size_t(index)].fill});
    }
    for (int i = 0; i < plan.idleSteps; ++i) {
        round.push_back(Step{nullptr, m_idleTasks[size_t(i)].get()});
    }
    auto run = [this](Step& step) {
        if (step.stream) {
            // The fill to show: after a seek what the seek filled, otherwise
            // what the stream had when the round looked at it (its low point,
            // before this step refilled or flushed it); none out of sync
            const int fill = step.stream->step(m_round);
            step.fill = fill < 0 ? -1 : (m_round.seek ? fill : step.fill);
        } else if (!m_seekRequested.load(std::memory_order_relaxed)) {
            step.over = !step.idle->step();
        }
    };

    if (round.size() == 1) {
        // Nothing to share: no hand-over to the pool
        run(round.front());
    } else if (!round.empty()) {
        // The pool runs the steps; the round ends when the last one is done. A
        // task takes its steps on different threads from round to round: the
        // steps see what this thread wrote before the round, this thread (and
        // the next round) sees what they wrote. blockingMap synchronises
        // inside Qt; these two atomics say so in a way a thread sanitizer
        // sees too.
        m_roundStarted.fetch_add(1, std::memory_order_release);
        QtConcurrent::blockingMap(round, [this, &run](Step& step) {
            m_roundStarted.load(std::memory_order_acquire);
            run(step);
            m_stepsDone.fetch_add(1, std::memory_order_release);
        });
        m_stepsDone.load(std::memory_order_acquire);
    }

    // For the GUI: the emptiest stream in sync since it last asked (a
    // reading it takes meanwhile may miss this round's low: it is a meter)
    int lowest = 100;
    for (const Step& step : round) {
        if (step.stream && step.fill >= 0) {
            lowest = std::min(lowest, step.fill);
        }
    }
    if (lowest < m_bufferFillStatus.load(std::memory_order_relaxed)) {
        m_bufferFillStatus.store(lowest, std::memory_order_relaxed);
    }

    if (seek) {
        // Where the streams seeked to
        m_transportLocation.store(m_round.seekLocation.universal_frame(), std::memory_order_relaxed);
        emit seekFinished();
    }

    // Idle tasks that are over go (a build reports its result then)
    for (const Step& step : round) {
        if (step.over) {
            step.idle->retire();
            std::erase_if(m_idleTasks, [&step](const std::unique_ptr<TIOTask>& task) { return task.get() == step.idle; });
        }
    }

    return plan.continueWithoutTick;
}

int TIOScheduler::idle_limit_for(const QString& directory)
{
#if defined(Q_OS_LINUX)
    // The block device of the directory's file system (of the nearest
    // existing directory above it: a new project's directories may not be
    // there yet); a partition has no queue of its own, its disk is the
    // directory above it
    QString existing = QFileInfo(directory).absoluteFilePath();
    while (!QFileInfo::exists(existing)) {
        const QString parent = QFileInfo(existing).absolutePath();
        if (parent == existing) {
            break;
        }
        existing = parent;
    }
    struct stat info;
    if (stat(QFile::encodeName(existing).constData(), &info) == 0) {
        const QString device = QStringLiteral("/sys/dev/block/%1:%2").arg(major(info.st_dev)).arg(minor(info.st_dev));
        for (const QString& queue : {device + QStringLiteral("/queue/rotational"), device + QStringLiteral("/../queue/rotational")}) {
            QFile file(queue);
            if (file.open(QIODevice::ReadOnly)) {
                const QByteArray rotational = file.readAll().trimmed();
                if (rotational == "0") {
                    return QThread::idealThreadCount();
                }
                if (rotational == "1") {
                    return 1;
                }
            }
        }
    }
    // Unknown (a file system without a block device of its own, like a
    // btrfs subvolume or a network share): as if it seeks
    return 1;
#else
    // Windows (seek penalty, IOCTL_STORAGE_QUERY_PROPERTY) and macOS (IOKit
    // "Medium Type") could tell; not done yet, so as if it seeks
    Q_UNUSED(directory);
    return 1;
#endif
}

void TIOScheduler::add_idle_task(std::unique_ptr<TIOTask> task)
{
    {
        std::lock_guard locker(m_requestsMutex);
        m_tasksToAdd.push_back(std::move(task));
    }
    // Also without a driver: the task does not wait for a tick
    m_wake.signal();
}

void TIOScheduler::add_stream(TIOStream* stream)
{
    PENTER2;

    Q_ASSERT(stream);
    if (stream->get_channel_count() == 0) {
        PMESG("TIOScheduler::add_stream: the stream has no channels, not adding it");
        return;
    }

    stream->set_output_rate_and_convertor_type(int(get_output_rate()), get_resample_quality());
    stream->prepare_rt_buffers(m_bufferSize.load(std::memory_order_relaxed));

    {
        std::lock_guard locker(m_requestsMutex);
        m_streamsToAdd.push_back(stream);
    }
    m_wake.signal();
}

void TIOScheduler::remove_stream(TIOStream* stream)
{
    PENTER2;

    std::unique_lock locker(m_requestsMutex);
    m_streamsToRemove.push_back(stream);
    if (!m_thread) {
        // No scheduler thread: this thread is the only one
        locker.unlock();
        take_requests();
        return;
    }
    // Until the scheduler thread took it, between two rounds: no step of it runs after
    const quint64 request = ++m_requested;
    m_wake.signal();
    m_requestsTaken.wait(locker, [this, request] { return m_taken >= request; });
}

/**
 *
 * @return Returns the CPU time consumed by the TIOScheduler thread
 */
bool TIOScheduler::get_cpu_time(float &time)
{
    trav_time_t currentTime = TTimeRef::get_nanoseconds_since_epoch();

    // exchange: what the scheduler thread adds meanwhile counts next time
    time = (m_doWorktTime.exchange(0) / float(currentTime - m_lastCpuReadTime)) * 100;

    m_lastCpuReadTime = currentTime;

    return true;
}


/**
 * 	Get the status of the writebuffers.
 *
 * @return The status in procentual amount of the smallest remaining space in the writebuffers
 *		that could be used to write 'recording' data too.
 */
int TIOScheduler::get_buffers_fill_status( )
{
    return m_bufferFillStatus.exchange(100);
}

void TIOScheduler::set_resample_quality(int quality)
{
    m_resampleQuality.store(quality);
    m_qualityChanged.store(true);
}

void TIOScheduler::set_output_sample_rate(uint outputSampleRate)
{
    TLOG_I("diskio", "output sample rate {}", outputSampleRate);
    m_outputRate.store(outputSampleRate);
    m_rateChanged.store(true);
}
