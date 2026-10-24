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
#include <memory>
#include <mutex>
#include <vector>

#include "TIOGroup.h"
#include "TIOPlanner.h"
#include "TIOStream.h"
#include "TIOIdleTask.h"
#include "TTimeRef.h"
#include "defines.h"

#include "cameron/atomicops.h"

/**
 * @brief Reads and writes the buffered audio streams of the songs, and runs
 * background work (peak builds) in the idle time (doc/design/diskio-rounds.md,
 * dispatcher-conclusie.md).
 *
 * The application has two (TProjectManager): one for the readers and the
 * idle work, one for the writers (recordings), so a slow read never holds up
 * writing what was recorded. Every sheet is a group (TIOGroup) in the read
 * one. Each has a scheduler thread and a pool of worker threads of its own.
 * The scheduler thread waits for a wake (the audio thread's tick every
 * cycle, a request, a step that is done) and then dispatches: it takes the
 * requests and the reports of the steps that are done, asks TIOPlanner for
 * the order of what may start, and starts one step of each on the pool, as
 * many as there are threads free. A step that is done reports back and the
 * next one starts at once: no step waits for a slower one (no rounds). A
 * stream or a task has at most one step at a time; a stream at most one
 * refill step per tick; idle work never takes the last thread. Nothing is
 * timed for the plan: the kernel schedules, the scheduler only knows the
 * order.
 *
 * Threads (doc/design/diskio-rounds.md, section 2, has the full contract):
 * - audio thread: tick(), the transport and the seek of a group
 *   (TIOGroup); never blocks (atomics, the wake). It reads or writes the
 *   rings of the streams it processes.
 * - owner thread (GUI): the constructor, start(), the destructor,
 *   add_group(), remove_group(), add_stream(), remove_stream(), the meters.
 *   remove_stream() waits until the scheduler thread let go of the stream:
 *   at once, or after the step it has running.
 * - any thread: add_idle_task() and the setters (atomics, or the request
 *   lock for a moment).
 * - scheduler thread: dispatch(); it alone has the streams, the tasks, the
 *   groups' seeks, the planner and what runs, in plain variables. It blocks
 *   on the wake only, and never runs a step.
 * - worker threads: step() of one stream or one task, then the report.
 *
 * Hand-overs, four: the wake (a semaphore), the requests (a lock, swapped
 * out, and a wake), a step to the pool (QThreadPool::start()), and a step
 * done (a lock, swapped out, and a wake).
 *
 * Ownership: a stream is its owner's until add_stream() and again once
 * remove_stream() returned; in between the scheduler has it, and calls
 * retire() when it takes it out. The owner then deletes it. Before
 * remove_stream() the owner takes it out of the audio processing. A task is
 * the scheduler's from add_idle_task() until it is retired and deleted. A
 * group is its owner's; the scheduler reads it between add_group() and
 * remove_group(). One seek at a time per group: the next is asked for after
 * seekFinished(group).
 *
 * No deadlock in remove_stream(): the owner holds no lock there that a step
 * needs, and a step never waits for the owner thread.
 *
 * The object lives on the owner thread; only dispatch() runs on the
 * scheduler thread. Without start() there is no scheduler thread: a test
 * calls dispatch() itself, which then waits until the steps it started are
 * done.
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
    // thread and a request never wait for each other. The count of ticks
    // gives every stream one refill step per tick.
    void tick()
    {
        m_ticks.fetch_add(1, std::memory_order_relaxed);
        m_wake.signal();
    }

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
    // driver: its next step starts once its step before reported.
    void add_idle_task(std::unique_ptr<TIOIdleTask> task);

    // Any thread. The output rate takes effect at a stream's next seek (the
    // owner seeks after changing it), the resampler at the next dispatch (or
    // after the step that runs), for every stream at the rate it reads at.
    void set_output_sample_rate(uint outputSampleRate);
    void set_resampler(const TIOStream::Resampler& resampler);
    // The audio device's buffer size: the ring buffers of new streams, and
    // of a stream at its next seek once the rate or the buffer size changed
    void set_buffer_size(nframes_t bufferSize) { m_bufferSize.store(bufferSize, std::memory_order_relaxed); }
    uint get_output_rate() const { return m_outputRate.load(std::memory_order_relaxed); }
    TIOStream::Resampler get_resampler() const { return m_resampler.load(std::memory_order_relaxed); }

    // At most this many idle steps at a time (1 on a disk that seeks), with
    // no song; with songs the lowest of theirs (TIOGroup::set_idle_limit()).
    // Any thread.
    void set_idle_limit(int limit) { m_idleLimit.store(limit, std::memory_order_relaxed); }
    int idle_limit() const { return m_idleLimit.load(std::memory_order_relaxed); }
    // The idle limit for sources in directory: 1 on a hard disk, which a
    // second read at the same time sends seeking; as many as there are
    // threads on an SSD. Known on Linux only; elsewhere, and when in doubt, 1.
    static int idle_limit_for(const QString& directory);

    // The GUI's meters
    // The wall-clock time the steps took since the last call, in percent of
    // the time that passed (of one core; waiting for the disk counts too)
    bool get_step_load(float &time);
    // The fill of the emptiest stream since the last call, in percent (a
    // writer's fill is its free part), for readers and writers alike; 100
    // when no step reported meanwhile
    int lowest_buffer_fill();

    // One turn of the scheduler thread: the requests and the reports that
    // came, then the steps there are threads for. True when idle steps were
    // started. On the scheduler thread, or on the owner's thread without one
    // (a test): it waits then until the steps it started are done.
    bool dispatch();

signals:
    // The seek of the group is done: all its streams seeked
    void seekFinished(TIOGroup* group);

private:
    // A stream as the scheduler thread has it; its address stays, a step's
    // report points to it
    struct StreamWork;
    // An idle task as the scheduler thread has it
    struct TaskWork
    {
        std::unique_ptr<TIOIdleTask> task;
        bool inFlight{false};
        qint64 weight{0};               // its weight when its step started
    };
    // A song as the scheduler thread has it
    struct GroupWork
    {
        TIOGroup* group{nullptr};
        bool seeking{false};
        int seeksLeft{0};               // its streams whose seek step is to come
    };
    // A removal the owner waits for (on its stack)
    struct Removal
    {
        TIOStream* stream{nullptr};
        TIOGroup* group{nullptr};
        bool done{false};               // under the request lock
    };
    struct StreamWork
    {
        TIOStream* stream{nullptr};
        TIOGroup* group{nullptr};       // none: a writer
        uint rate{0};                   // the output rate it reads at
        nframes_t bufferSize{0};        // the buffer size of its rings
        bool seek{false};               // its seek step is to come
        bool inFlight{false};           // a step of it runs
        bool resamplerPending{false};   // a new resampler, for after the step that runs
        quint64 stepTick{~quint64(0)};  // the tick of its last refill step
        Removal* removal{nullptr};      // removed once the step that runs is done
    };
    // What a step tells when it is done, from its worker thread
    struct Report
    {
        StreamWork* stream{nullptr};
        TaskWork* task{nullptr};
        TIOStream::StepInfo info;
        int fill{-1};                   // what the GUI is told, -1 for nothing
        bool over{false};               // the task is over
        bool idle{false};
        trav_time_t nanoseconds{0};     // how long the step took
    };

    void run();
    // The owners' requests: groups and streams added and removed, new idle
    // tasks
    void take_requests();
    // The steps that are done
    void take_reports();
    // The steps there are threads for, in the planner's order; true when
    // idle steps were started
    bool start_steps();
    void start_stream_step(StreamWork* work, bool idle, int plannedFill, const TIOStream::StepInfo& info, quint64 tick);
    void start_task_step(TaskWork* task);
    void report(const Report& report);
    // Takes the stream out: its song has one stream less to seek
    void retire_stream(StreamWork* work);
    void finish_seeks();
    bool on_scheduler_thread() const;

    std::unique_ptr<QThread> m_thread;
    // The worker threads, of this scheduler alone
    QThreadPool m_pool;
    moodycamel::spsc_sema::LightweightSemaphore m_wake;
    std::atomic<bool> m_stopRequested{false};
    std::atomic<quint64> m_ticks{0};

    // The owners' requests (std::mutex: a thread sanitizer sees it, Qt's own
    // here is not built for one)
    std::mutex m_requestsMutex;
    std::condition_variable m_removalDone;   // a removal is done
    std::vector<TIOGroup*> m_groupsToAdd;
    std::vector<StreamWork> m_streamsToAdd;
    std::vector<Removal*> m_removals;
    std::vector<std::unique_ptr<TIOIdleTask>> m_tasksToAdd;

    // The steps that are done
    std::mutex m_reportsMutex;
    std::vector<Report> m_reports;
    // A step handed to the pool: QThreadPool's own lock orders it; this
    // fence (release before the hand-over, acquire in the step) says so in a
    // way a thread sanitizer sees too. It counts nothing anyone reads.
    // Static: the step reads it before anything of its closure.
    static std::atomic<quint64> s_handOverFence;

    std::atomic<uint> m_outputRate{0};
    std::atomic<TIOStream::Resampler> m_resampler{};  // the owner sets it before streams come
    std::atomic<bool> m_resamplerChanged{false};
    std::atomic<nframes_t> m_bufferSize{0};

    std::atomic<int> m_lowestBufferFill{0};
    // The time the steps took (get_step_load())
    std::atomic<trav_time_t> m_stepTime{0};
    trav_time_t m_lastStepLoadTime;
    std::atomic<int> m_idleLimit{1};

    // The scheduler thread's
    TIOPlanner m_planner;
    std::vector<GroupWork> m_groups;
    std::vector<std::unique_ptr<StreamWork>> m_streams;
    std::vector<std::unique_ptr<TaskWork>> m_idleTasks;     // in the order they came
    int m_running{0};                   // steps that run
    int m_runningIdle{0};               // of them idle steps

    // The step information of a stream's step
    TIOStream::StepInfo step_info(const StreamWork& work, uint outputRate, nframes_t bufferSize,
                                  const TIOStream::Resampler& resampler) const;
};
