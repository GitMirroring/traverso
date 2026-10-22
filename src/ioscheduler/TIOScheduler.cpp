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
#include <climits>

#if defined (Q_OS_LINUX)
// idle_limit_for(): the block device of a directory
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#endif

TIOScheduler::TIOScheduler(int threads)
    : m_lastCpuReadTime(TTimeRef::get_nanoseconds_since_epoch())
{
    m_pool.setMaxThreadCount(std::max(threads, 2));
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
    Q_ASSERT_X(m_groups.empty() && m_groupsToAdd.empty(), "TIOScheduler", "groups are still registered");
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

        more = dispatch();
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

    // Under the lock only the swap: a request never waits for retire()
    std::vector<TIOGroup*> groupsAdded;
    std::vector<TIOGroup*> groupsRemoved;
    std::vector<StreamWork> added;
    std::vector<TIOStream*> removed;
    std::vector<std::unique_ptr<TIOTask>> tasks;
    quint64 requested = 0;
    {
        std::lock_guard locker(m_requestsMutex);
        groupsAdded.swap(m_groupsToAdd);
        groupsRemoved.swap(m_groupsToRemove);
        added.swap(m_streamsToAdd);
        removed.swap(m_streamsToRemove);
        tasks.swap(m_tasksToAdd);
        requested = m_requested;
    }

    for (TIOGroup* group : groupsAdded) {
        m_groups.push_back(GroupWork{group});
    }
    for (const StreamWork& work : added) {
        Q_ASSERT(std::none_of(m_streams.begin(), m_streams.end(), [&work](const StreamWork& w) { return w.stream == work.stream; }));
        m_streams.push_back(work);
    }
    // The ring buffers go here, where the audio thread does not use them;
    // the owner deletes the stream once remove_stream() returns. A stream
    // that was still to seek is one less for its song.
    for (TIOStream* stream : removed) {
        const auto it = std::find_if(m_streams.begin(), m_streams.end(), [stream](const StreamWork& w) { return w.stream == stream; });
        if (it == m_streams.end()) {
            continue;
        }
        if (it->seek) {
            for (GroupWork& group : m_groups) {
                if (group.group == it->group) {
                    --group.seeksLeft;
                }
            }
        }
        m_streams.erase(it);
        stream->retire();
    }
    for (TIOGroup* group : groupsRemoved) {
        Q_ASSERT_X(std::none_of(m_streams.begin(), m_streams.end(), [group](const StreamWork& w) { return w.group == group; }),
                   "TIOScheduler::remove_group", "the group still has streams");
        std::erase_if(m_groups, [group](const GroupWork& w) { return w.group == group; });
    }
    for (std::unique_ptr<TIOTask>& task : tasks) {
        m_idleTasks.push_back(std::move(task));
    }

    // remove_stream() and remove_group() return now
    if (!removed.empty() || !groupsRemoved.empty()) {
        {
            std::lock_guard locker(m_requestsMutex);
            m_taken = requested;
        }
        m_requestsTaken.notify_all();
    }
}

bool TIOScheduler::playing_seek_requested() const
{
    for (const GroupWork& group : m_groups) {
        if (group.group->m_seekRequested.load(std::memory_order_relaxed) && group.group->m_playing.load(std::memory_order_relaxed)) {
            return true;
        }
    }
    return false;
}

TIOStream::StepInfo TIOScheduler::step_info(const StreamWork& work, uint outputRate, nframes_t bufferSize,
                                           const TIOStream::Resampler& resampler) const
{
    TIOStream::StepInfo info;
    // A writer has no song: no transport, no seek
    if (work.group) {
        info.transportLocation = TTimeRef(work.group->m_transportLocation.load(std::memory_order_relaxed));
    }
    info.seek = work.seek;
    info.seekLocation = info.transportLocation;
    // A new rate or buffer size goes with the stream's next seek: its rings
    // are made again there, where the audio thread does not use them
    info.sampleRateChanged = work.seek && (work.rate != outputRate || work.bufferSize != bufferSize);
    info.outputRate = outputRate;
    info.resampler = resampler;
    info.bufferSize = work.seek ? bufferSize : 0;
    return info;
}

bool TIOScheduler::do_round()
{
    Q_ASSERT_X(on_scheduler_thread(), "TIOScheduler::do_round", "not on the scheduler thread");

    const uint outputRate = get_output_rate();
    const nframes_t bufferSize = m_bufferSize.load(std::memory_order_relaxed);
    const TIOStream::Resampler resampler = get_resampler();

    // A new resampler: every stream at the rate it reads at now (a new rate
    // comes with the stream's next seek)
    if (m_resamplerChanged.exchange(false)) {
        for (StreamWork& work : m_streams) {
            work.stream->set_output_rate_and_resampler(int(work.rate), resampler);
        }
    }

    // A song's seek is taken at the start of a round (the location is written
    // before the flag): its streams seek to the location, which is where its
    // transport is from now on (the audio thread does not move it while the
    // song seeks)
    for (GroupWork& group : m_groups) {
        if (!group.group->m_seekRequested.exchange(false, std::memory_order_acquire)) {
            continue;
        }
        group.group->m_transportLocation.store(group.group->m_seekLocation.load(std::memory_order_relaxed),
                                               std::memory_order_relaxed);
        group.seeking = true;
        group.seeksLeft = 0;
        for (StreamWork& work : m_streams) {
            if (work.group == group.group) {
                work.seek = true;
                ++group.seeksLeft;
            }
        }
        TLOG_D("diskio", "seek requested, {} streams seek", group.seeksLeft);
    }

    // What every stream needs; a stream that seeks needs its seek. An idle
    // task has work until its step says it is over.
    std::vector<TIOPlanner::Stream> streams;
    streams.reserve(m_streams.size());
    bool idleAllowed = true;
    int idleLimit = m_groups.empty() ? m_idleLimit.load(std::memory_order_relaxed) : INT_MAX;
    for (const GroupWork& group : m_groups) {
        idleAllowed = idleAllowed && !group.group->m_recording.load(std::memory_order_relaxed);
        idleLimit = std::min(idleLimit, group.group->idle_limit());
    }
    for (const StreamWork& work : m_streams) {
        const bool playing = work.group && work.group->m_playing.load(std::memory_order_relaxed);
        if (work.seek) {
            streams.push_back(TIOPlanner::Stream{false, false, 100, true, playing});
            continue;
        }
        const TTimeRef transport = work.group ? TTimeRef(work.group->m_transportLocation.load(std::memory_order_relaxed)) : TTimeRef();
        TIOPlanner::Stream state = work.stream->disk_state(transport);
        state.playing = playing;
        streams.push_back(state);
    }

    // blockingMap() starts the round on the pool and waits: the pool's
    // threads do the work, and this thread takes part in it
    const TIOPlanner::Round plan = m_planner.plan(streams, int(m_idleTasks.size()), idleAllowed,
                                                  m_pool.maxThreadCount(), idleLimit);

    // A step of a stream (fill: what it had when the round looked at it,
    // afterwards what the GUI is told, -1 for nothing) or of an idle task.
    // Idle steps (the seeks of songs that do not play, the idle tasks) that
    // have not started yet give way to a seek of a song that plays that comes
    // in meanwhile (they get their step in a later round); a read that runs
    // already finishes first.
    struct Step
    {
        StreamWork* work{nullptr};
        TIOTask* idle{nullptr};
        TIOStream::StepInfo info;
        bool idleStep{false};
        bool done{false};
        int fill{-1};
        bool over{false};
        trav_time_t nanoseconds{0};     // how long the step took
    };
    std::vector<Step> round;
    round.reserve(plan.streams.size() + plan.idleSeeks.size() + size_t(plan.idleSteps));
    for (const int index : plan.streams) {
        StreamWork& work = m_streams[size_t(index)];
        round.push_back(Step{&work, nullptr, step_info(work, outputRate, bufferSize, resampler), false, false, streams[size_t(index)].fill});
    }
    for (const int index : plan.idleSeeks) {
        StreamWork& work = m_streams[size_t(index)];
        round.push_back(Step{&work, nullptr, step_info(work, outputRate, bufferSize, resampler), true, false, streams[size_t(index)].fill});
    }
    for (int i = 0; i < plan.idleSteps; ++i) {
        round.push_back(Step{nullptr, m_idleTasks[size_t(i)].get(), {}, true});
    }
    auto run = [this](Step& step) {
        const trav_time_t start = TTimeRef::get_nanoseconds_since_epoch();
        if (step.idleStep && playing_seek_requested()) {
            // Its turn comes again
        } else if (step.work) {
            // The fill to show: after a seek what the seek filled, otherwise
            // what the stream had when the round looked at it (its low point,
            // before this step refilled or flushed it); none out of sync
            const int fill = step.work->stream->step(step.info);
            step.fill = fill < 0 ? -1 : (step.info.seek ? fill : step.fill);
            step.done = true;
        } else {
            step.over = !step.idle->step();
            step.done = true;
        }
        step.nanoseconds = TTimeRef::get_nanoseconds_since_epoch() - start;
    };

    const trav_time_t roundStart = TTimeRef::get_nanoseconds_since_epoch();
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
        QtConcurrent::blockingMap(&m_pool, round, [this, &run](Step& step) {
            m_roundStarted.load(std::memory_order_acquire);
            run(step);
            m_stepsDone.fetch_add(1, std::memory_order_release);
        });
        m_stepsDone.load(std::memory_order_acquire);
    }

    // The CPU time is the time the steps took, on whatever thread
    trav_time_t stepTime = 0;
    quint64 idleSteps = 0;
    for (const Step& step : round) {
        stepTime += step.nanoseconds;
        idleSteps += step.idleStep && step.done ? 1 : 0;
    }
    m_doWorktTime.fetch_add(stepTime, std::memory_order_relaxed);
    if (idleSteps > 0 || m_idleStretch.start != 0) {
        if (m_idleStretch.start == 0) {
            m_idleStretch.start = roundStart;
        }
        m_idleStretch.stepTime += stepTime;
        m_idleStretch.steps += round.size();
        m_idleStretch.idleSteps += idleSteps;
    }

    // For the GUI: the emptiest stream in sync since it last asked (a
    // reading it takes meanwhile may miss this round's low: it is a meter)
    int lowest = 100;
    for (const Step& step : round) {
        if (step.work && step.done && step.fill >= 0) {
            lowest = std::min(lowest, step.fill);
        }
    }
    if (lowest < m_bufferFillStatus.load(std::memory_order_relaxed)) {
        m_bufferFillStatus.store(lowest, std::memory_order_relaxed);
    }

    // The seeks that were done: the stream reads at the rate and with the
    // rings of the seek, and its song has one stream less to seek
    for (const Step& step : round) {
        if (!step.work || !step.done || !step.info.seek) {
            continue;
        }
        step.work->seek = false;
        step.work->rate = step.info.outputRate;
        step.work->bufferSize = step.info.bufferSize;
        for (GroupWork& group : m_groups) {
            if (group.group == step.work->group) {
                --group.seeksLeft;
            }
        }
    }
    for (GroupWork& group : m_groups) {
        if (group.seeking && group.seeksLeft <= 0) {
            group.seeking = false;
            group.group->m_seeking.store(false, std::memory_order_relaxed);
            emit seekFinished(group.group);
        }
    }

    // Idle tasks that are over go (a build reports its result then)
    for (const Step& step : round) {
        if (step.over) {
            step.idle->retire();
            std::erase_if(m_idleTasks, [&step](const std::unique_ptr<TIOTask>& task) { return task.get() == step.idle; });
        }
    }

    // The idle work is over: how long it took, and how much of the threads
    // the steps used meanwhile (the steps' time against the threads' time)
    if (m_idleTasks.empty() && m_idleStretch.start != 0) {
        const trav_time_t duration = TTimeRef::get_nanoseconds_since_epoch() - m_idleStretch.start;
        const int threads = m_pool.maxThreadCount();
        const double used = duration > 0 ? 100.0 * double(m_idleStretch.stepTime) / (double(threads) * double(duration)) : 0.0;
        TLOG_I("diskio", "idle work done in {:.3f} s: {} idle steps, {} steps, {} threads, {:.0f}% used",
               double(duration) / 1e9, m_idleStretch.idleSteps, m_idleStretch.steps, threads, used);
        m_idleStretch = IdleStretch{};
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

void TIOScheduler::add_group(TIOGroup* group)
{
    Q_ASSERT(group);
    {
        std::lock_guard locker(m_requestsMutex);
        m_groupsToAdd.push_back(group);
    }
    m_wake.signal();
}

void TIOScheduler::remove_group(TIOGroup* group)
{
    std::unique_lock locker(m_requestsMutex);
    m_groupsToRemove.push_back(group);
    if (!m_thread) {
        // No scheduler thread: this thread is the only one
        locker.unlock();
        take_requests();
        return;
    }
    // Until the scheduler thread took it, between two rounds
    const quint64 request = ++m_requested;
    m_wake.signal();
    m_requestsTaken.wait(locker, [this, request] { return m_taken >= request; });
}

void TIOScheduler::add_stream(TIOStream* stream, TIOGroup* group)
{
    PENTER2;

    Q_ASSERT(stream);
    if (stream->get_channel_count() == 0) {
        PMESG("TIOScheduler::add_stream: the stream has no channels, not adding it");
        return;
    }

    const uint rate = get_output_rate();
    const nframes_t bufferSize = m_bufferSize.load(std::memory_order_relaxed);
    stream->set_output_rate_and_resampler(int(rate), get_resampler());
    stream->prepare_rt_buffers(bufferSize);

    {
        std::lock_guard locker(m_requestsMutex);
        m_streamsToAdd.push_back(StreamWork{stream, group, rate, bufferSize});
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

void TIOScheduler::set_resampler(const TIOStream::Resampler& resampler)
{
    m_resampler.store(resampler);
    m_resamplerChanged.store(true);
}

void TIOScheduler::set_output_sample_rate(uint outputSampleRate)
{
    TLOG_I("diskio", "output sample rate {}", outputSampleRate);
    m_outputRate.store(outputSampleRate);
}
