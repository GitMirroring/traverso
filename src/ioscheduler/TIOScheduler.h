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

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

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
 * recorded. Both run the same loop on a thread of their own: wait for a
 * wake (the audio thread's tick every cycle, or a request), then rounds
 * (dispatch()). A round asks every stream what it needs, lets TIOPlanner
 * pick the order (streams first, the emptiest first, then idle tasks on the
 * threads left) and runs one step of each in parallel on the global thread
 * pool (the loop waits, and runs the round itself when the pool is busy).
 * Between rounds, never during one: seeks, streams added and removed, new
 * idle tasks, rate changes. Nothing is timed: the kernel schedules, the
 * scheduler only knows the order.
 *
 * The object lives on the thread that made it (the GUI thread); only its
 * loop runs on the loop thread. Without start() there is no loop: a test
 * calls dispatch() itself.
 */
class TIOScheduler : public QObject
{
    Q_OBJECT

public:
    TIOScheduler();
    // Stops the loop and hands back the tasks that are left. The streams
    // must have been removed before.
    ~TIOScheduler();

    // Starts the loop thread, once the owner has set the rate, the buffer
    // size and the quality
    void start();

    // The audio thread, every cycle: wakes the loop, never blocks
    void tick() { m_wake.signal(); }
    void set_transport_location(const TTimeRef& transportLocation) {
        m_transportLocation.store(transportLocation.universal_frame(), std::memory_order_relaxed);
    }
    void set_seek_transport_location(const TTimeRef& transportLocation) {
        m_seekTransportLocation = transportLocation;
        m_seekRequested.store(true);
    }

    // The owner's thread. A stream to read or write. remove_stream()
    // returns once the loop has let go of the stream (it called retire());
    // the owner deletes it then.
    void add_stream(TIOStream* stream);
    void remove_stream(TIOStream* stream);

    // Background work for the idle time; the scheduler owns the task until
    // it is done (and deletes it then, or when it goes). Any thread. Picked
    // up at the next wake: without a running driver (not even the Dummy
    // one) the loop is not woken every cycle and idle tasks wait.
    void add_idle_task(std::unique_ptr<TIOTask> task);

    // Any thread. The output rate takes effect at the next seek (the
    // owner seeks after changing it), the quality at the next round.
    void set_output_sample_rate(uint outputSampleRate);
    void set_resample_quality(int quality);
    // The audio device's buffer size: the ring buffers of new streams, and
    // of all streams after a seek with a new rate
    void set_buffer_size(nframes_t bufferSize) { m_bufferSize.store(bufferSize, std::memory_order_relaxed); }
    uint get_output_rate() const { return m_outputRate.load(std::memory_order_relaxed); }
    int get_resample_quality() const { return m_resampleQuality.load(std::memory_order_relaxed); }

    // false while the sheet records: the disk belongs to the recording. Per
    // sheet: the other sheets go on (peak files are rebuilt only after a new
    // file version or when they are lost, and nobody records then)
    void set_idle_work_allowed(bool allowed) { m_idleWorkAllowed.store(allowed, std::memory_order_relaxed); }
    // At most this many idle tasks per round (1 on a disk that seeks). Any thread.
    void set_idle_limit(int limit) { m_idleLimit.store(limit, std::memory_order_relaxed); }
    int idle_limit() const { return m_idleLimit.load(std::memory_order_relaxed); }
    // The idle limit for sources in directory: 1 on a hard disk, which a
    // second read at the same time sends seeking; as many as there are
    // threads on an SSD. Known on Linux only; elsewhere, and when in doubt, 1.
    static int idle_limit_for(const QString& directory);

    // The GUI's meters
    bool get_cpu_time(float &time);
    int get_buffers_fill_status();

    // One turn of the loop: the requests that came, then a round. True when
    // the next turn follows without waiting for a wake. On the loop thread,
    // or on the owner's thread when there is no loop (a test).
    bool dispatch();

signals:
    void seekFinished();

private:
    void loop();
    // Between rounds: streams added and removed, new idle tasks
    void take_requests();
    // One round; true when the next one follows without waiting for the tick
    bool do_round();
    bool on_loop_thread() const;

    std::unique_ptr<QThread> m_loop;
    moodycamel::spsc_sema::LightweightSemaphore m_wake;
    std::atomic<bool> m_stopRequested{false};

    // The owner's requests, taken between rounds (std::mutex: a thread
    // sanitizer sees it, Qt's own here is not built for one)
    std::mutex m_requestsMutex;
    std::condition_variable m_requestsTaken;
    std::vector<TIOStream*> m_streamsToAdd;
    std::vector<TIOStream*> m_streamsToRemove;
    std::vector<std::unique_ptr<TIOTask>> m_tasksToAdd;
    quint64 m_requested{0};
    quint64 m_taken{0};

    std::atomic<bool> m_seekRequested{false};
    std::atomic<qint64> m_transportLocation{0};     // universal frames
    TTimeRef m_seekTransportLocation;

    std::atomic<uint> m_outputRate{0};
    std::atomic<int> m_resampleQuality{0};          // the owner sets it before streams come
    std::atomic<bool> m_rateChanged{false};
    std::atomic<bool> m_qualityChanged{false};
    std::atomic<nframes_t> m_bufferSize{0};

    std::atomic<int> m_bufferFillStatus{0};
    std::atomic<trav_time_t> m_doWorktTime{0};
    trav_time_t m_lastCpuReadTime;

    // What the steps of the current round need to know
    TIOStream::StepInfo m_round;

    // The hand-over between the loop and the round's steps (do_round())
    std::atomic<quint64> m_roundStarted{0};
    std::atomic<quint64> m_stepsDone{0};

    // The loop's
    TIOPlanner m_planner;
    std::vector<TIOStream*> m_streams;
    std::deque<std::unique_ptr<TIOTask>> m_idleTasks;
    std::atomic<bool> m_idleWorkAllowed{true};
    std::atomic<int> m_idleLimit{1};
};
