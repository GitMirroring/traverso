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

#pragma once

#include <QObject>
#include <QThread>
#include <QThreadPool>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

#include "TIOGroup.h"
#include "TIOPlanner.h"
#include "TIOStream.h"
#include "TIOTask.h"
#include "TTimeRef.h"
#include "defines.h"

#include "cameron/atomicops.h"

/**
 * @brief Reads and writes the buffered audio streams of a sheet, and runs
 * background work (peak builds) in its idle time (doc/design/diskio-rounds.md).
 *
 * Each sheet has two: one for its readers and their idle work, one for its
 * writers (recordings), so a slow read never holds up writing what was
 * recorded. Each runs on a scheduler thread of its own: wait for a wake (the
 * audio thread's tick every cycle, or a request), then rounds (dispatch()).
 * A round asks every stream what it needs, lets TIOPlanner pick the order
 * (streams first, the emptiest first, then idle tasks on the threads left)
 * and runs one step of each in parallel on worker threads (the global pool;
 * the scheduler thread waits, and runs steps itself when the pool is busy).
 * Between rounds, never during one: seeks, streams added and removed, new
 * idle tasks, rate changes. Nothing is timed: the kernel schedules, the
 * scheduler only knows the order.
 *
 * Threads (doc/design/diskio-rounds.md, section 2, has the full contract):
 * - audio thread: tick(), set_transport_location(),
 *   set_seek_transport_location(); never blocks (atomics, the wake). It
 *   reads or writes the rings of the streams it processes.
 * - owner thread (GUI): the constructor, start(), the destructor,
 *   add_stream(), remove_stream(), the meters. remove_stream() waits until
 *   the round that runs is over.
 * - any thread: add_idle_task() and the setters (atomics, or the request
 *   lock for a moment).
 * - scheduler thread: dispatch(); it alone has the stream list, the task
 *   list, the planner and the round. It blocks on the wake, on the end of a
 *   round, and on disk IO when it runs a step itself.
 * - worker threads: step() of one stream or one task, during a round.
 *
 * Ownership: a stream is its owner's until add_stream() and again once
 * remove_stream() returned; in between the scheduler has it, and calls
 * retire() when it takes it out. The owner then deletes it. Before
 * remove_stream() the owner takes it out of the audio processing. A task is
 * the scheduler's from add_idle_task() until it is retired and deleted.
 * One seek at a time: the next is asked for after seekFinished.
 *
 * No deadlock in remove_stream(): the owner holds no lock there that a step
 * needs, and a step never waits for the owner thread.
 *
 * The object lives on the owner thread; only dispatch() runs on the
 * scheduler thread. Without start() there is no scheduler thread: a test
 * calls dispatch() itself.
 */
class TIOScheduler : public QObject
{
    Q_OBJECT

public:
    // threads: the worker threads of its pool, at least two
    explicit TIOScheduler(int threads = 2);
    // Stops the scheduler thread and hands back the tasks that are left. The streams
    // must have been removed before.
    ~TIOScheduler();

    // Starts the scheduler thread, once the owner has set the rate, the buffer
    // size and the quality
    void start();

    // The audio thread, every cycle: wakes the scheduler thread, never blocks.
    // The wake is a counting semaphore with one waiter (the scheduler
    // thread) and any number of wakers: an atomic add, and a post to the
    // system semaphore only when the scheduler thread waits. The audio
    // thread and a request never wait for each other.
    void tick() { m_wake.signal(); }

    // The owner's thread. A song (a sheet): its transport, its seek, whether
    // it plays or records (TIOGroup). Added before its streams, removed
    // after them; remove_group() returns once the scheduler thread has let
    // go of it.
    void add_group(TIOGroup* group);
    void remove_group(TIOGroup* group);

    // The owner's thread. A stream to read or write, of a song (a reader) or
    // of none (a writer: no transport, no seek). remove_stream() returns once
    // the scheduler thread has let go of the stream (it called retire()); the
    // owner deletes it then. The owner holds no lock a step needs while it
    // waits.
    void add_stream(TIOStream* stream, TIOGroup* group = nullptr);
    void remove_stream(TIOStream* stream);

    // Background work for the idle time; the scheduler owns the task until
    // it is done (and deletes it then, or when it goes). Any thread. Every
    // request wakes the scheduler thread, so a task runs also without a
    // driver (the rounds with idle work follow each other until it is over).
    void add_idle_task(std::unique_ptr<TIOTask> task);

    // Any thread. The output rate takes effect at a stream's next seek (the
    // owner seeks after changing it), the resampler at the next round, for
    // every stream at the rate it reads at.
    void set_output_sample_rate(uint outputSampleRate);
    void set_resampler(const TIOStream::Resampler& resampler);
    // The audio device's buffer size: the ring buffers of new streams, and
    // of a stream at its next seek once the rate or the buffer size changed
    void set_buffer_size(nframes_t bufferSize) { m_bufferSize.store(bufferSize, std::memory_order_relaxed); }
    uint get_output_rate() const { return m_outputRate.load(std::memory_order_relaxed); }
    TIOStream::Resampler get_resampler() const { return m_resampler.load(std::memory_order_relaxed); }

    // At most this many idle steps per round (1 on a disk that seeks), with
    // no song; with songs the lowest of theirs (TIOGroup::set_idle_limit()).
    // Any thread.
    void set_idle_limit(int limit) { m_idleLimit.store(limit, std::memory_order_relaxed); }
    int idle_limit() const { return m_idleLimit.load(std::memory_order_relaxed); }
    // The idle limit for sources in directory: 1 on a hard disk, which a
    // second read at the same time sends seeking; as many as there are
    // threads on an SSD. Known on Linux only; elsewhere, and when in doubt, 1.
    static int idle_limit_for(const QString& directory);

    // The GUI's meters
    bool get_cpu_time(float &time);
    int get_buffers_fill_status();

    // One turn of the scheduler thread: the requests that came, then a
    // round. True when the next turn follows without waiting for a wake. On
    // the scheduler thread, or on the owner's thread without one (a test).
    bool dispatch();

signals:
    // The seek of the group is done: all its streams seeked
    void seekFinished(TIOGroup* group);

private:
    void run();
    // Between rounds: streams added and removed, new idle tasks
    void take_requests();
    // One round; true when the next one follows without waiting for the tick
    bool do_round();
    bool on_scheduler_thread() const;

    std::unique_ptr<QThread> m_thread;
    // The worker threads, of this scheduler alone
    QThreadPool m_pool;
    moodycamel::spsc_sema::LightweightSemaphore m_wake;
    std::atomic<bool> m_stopRequested{false};

    // The owner's requests, taken between rounds (std::mutex: a thread
    // sanitizer sees it, Qt's own here is not built for one)
    std::mutex m_requestsMutex;
    std::condition_variable m_requestsTaken;
    // A stream as the scheduler thread has it
    struct StreamWork
    {
        TIOStream* stream{nullptr};
        TIOGroup* group{nullptr};       // none: a writer
        uint rate{0};                   // the output rate it reads at
        nframes_t bufferSize{0};        // the buffer size of its rings
        bool seek{false};               // its seek step is to come
    };
    // A song as the scheduler thread has it
    struct GroupWork
    {
        TIOGroup* group{nullptr};
        bool seeking{false};
        int seeksLeft{0};               // its streams whose seek step is to come
    };
    std::vector<TIOGroup*> m_groupsToAdd;
    std::vector<TIOGroup*> m_groupsToRemove;
    std::vector<StreamWork> m_streamsToAdd;
    std::vector<TIOStream*> m_streamsToRemove;
    std::vector<std::unique_ptr<TIOTask>> m_tasksToAdd;
    quint64 m_requested{0};
    quint64 m_taken{0};

    std::atomic<uint> m_outputRate{0};
    std::atomic<TIOStream::Resampler> m_resampler{};  // the owner sets it before streams come
    std::atomic<bool> m_resamplerChanged{false};
    std::atomic<nframes_t> m_bufferSize{0};

    std::atomic<int> m_bufferFillStatus{0};
    // The time the steps took (the CPU meter)
    std::atomic<trav_time_t> m_doWorktTime{0};
    trav_time_t m_lastCpuReadTime;

    // The hand-over between the scheduler thread and the round's steps (do_round())
    std::atomic<quint64> m_roundStarted{0};
    std::atomic<quint64> m_stepsDone{0};

    // The scheduler thread's
    // A stretch of idle work, from its first idle step until no idle task is
    // left, for the log: how long, and how much of the threads it used
    struct IdleStretch
    {
        trav_time_t start{0};
        trav_time_t stepTime{0};
        quint64 steps{0};
        quint64 idleSteps{0};
    };
    IdleStretch m_idleStretch;
    TIOPlanner m_planner;
    std::vector<GroupWork> m_groups;
    std::vector<StreamWork> m_streams;
    std::deque<std::unique_ptr<TIOTask>> m_idleTasks;
    std::atomic<int> m_idleLimit{1};

    // A seek of a song that plays was asked for (idle steps give way to it);
    // during a round, on the worker threads too (the groups do not change then)
    bool playing_seek_requested() const;
    // The step information of a stream's step
    TIOStream::StepInfo step_info(const StreamWork& work, uint outputRate, nframes_t bufferSize,
                                  const TIOStream::Resampler& resampler) const;
};
