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

#ifndef T_DISKIO_THREAD_H
#define T_DISKIO_THREAD_H

#include <QMutex>
#include <QThread>

#include <atomic>
#include <deque>
#include <memory>
#include <vector>

#include "TDiskIOPlanner.h"
#include "TDiskIOTask.h"
#include "TTimeRef.h"
#include "defines.h"

#include "cameron/readerwritercircularbuffer.h"

class TBufferedAudioStream;

/**
 * @brief Reads and writes the buffered audio streams of a sheet, and runs
 * background work (peak builds) in its idle time (doc/design/diskio-rounds.md).
 *
 * Each sheet has two: one for its readers and their idle work, one for its
 * writers (recordings), so a slow read never holds up writing what was
 * recorded. Both run the same loop: wait for the audio thread's tick (it
 * wakes them every cycle), then rounds. A round asks every stream what it
 * needs, lets TDiskIOPlanner pick the order (streams first, the
 * emptiest first, then idle tasks on the threads left) and runs one step of
 * each in parallel on the global thread pool (this thread waits, and runs
 * the round itself when the pool is busy). Between
 * rounds, never during one: seeks, streams added and removed, new idle
 * tasks, rate changes. Nothing is timed: the kernel schedules, DiskIO only
 * knows the order.
 */
class TDiskIOThread : public QThread
{
	Q_OBJECT

public:
    TDiskIOThread();
    ~TDiskIOThread();
	
    // The audio thread, every cycle
    void set_transport_location(const TTimeRef& transportLocation) {
        m_transportLocation.store(transportLocation.universal_frame(), std::memory_order_relaxed);
    }

    void set_seek_transport_location(const TTimeRef& transportLocation) {
        m_seekTransportLocation = transportLocation;
        m_seekRequested.store(true);
    }

    void add_buffered_audio_stream(TBufferedAudioStream* bufferedAudioStream);
    void remove_buffered_audio_stream(TBufferedAudioStream* bufferedAudioStream);

    bool get_cpu_time(float &time);
    int get_buffers_fill_status();
    uint get_output_rate() {return m_outputSampleRate;}
	int get_resample_quality() {return m_resampleQuality;}

    void add_processed_audio_thread_frames(nframes_t nframes)
    {
        m_audioThreadProcessedFramesQueue->try_enqueue(nframes);
    }
    void wakeup()
    {
        m_audioThreadProcessedFramesQueue->try_enqueue(0);
    }

    // Background work for the idle time; DiskIO owns the task until it is
    // done (and deletes it then, or when DiskIO goes). Any thread. Picked up
    // at the next tick: without a running driver (not even the Dummy one)
    // DiskIO is not woken and idle tasks do not run.
    void add_idle_task(std::unique_ptr<TDiskIOTask> task);

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

protected:
    void run() override;

private:
    std::unique_ptr<moodycamel::BlockingReaderWriterCircularBuffer<nframes_t>>      m_audioThreadProcessedFramesQueue;
    std::unique_ptr<moodycamel::BlockingReaderWriterCircularBuffer<TBufferedAudioStream*>>   m_streamsToBeAdded;
    std::unique_ptr<moodycamel::BlockingReaderWriterCircularBuffer<TBufferedAudioStream*>>   m_streamsToBeRemoved;

    std::atomic<bool>   m_seekRequested;
    std::atomic<bool>   m_stopDiskIOThreadRequested;

    std::atomic<int>    m_bufferFillStatus;
    std::atomic<trav_time_t> m_doWorktTime;
    trav_time_t         m_lastCpuReadTime;

    int                 m_resampleQuality;
    bool                m_resampleQualityChanged;
    bool                m_sampleRateChanged;

    uint                m_outputSampleRate{};

    std::atomic<qint64> m_transportLocation{0};     // universal frames
    TTimeRef            m_seekTransportLocation;

    // What the steps of the current round need to know (this thread writes
    // it before the round, the steps only read it)
    struct Round
    {
        bool seek{false};
        TTimeRef seekLocation;
        TTimeRef transportLocation;
        bool sampleRateChanged{false};
        uint outputRate{0};
        int resampleQuality{0};
        nframes_t bufferSize{0};
    };
    Round m_round;

    // The hand-over between this thread and the round's steps (do_round())
    std::atomic<quint64> m_roundStarted{0};
    std::atomic<quint64> m_stepsDone{0};

    TDiskIOPlanner m_planner;
    std::vector<TBufferedAudioStream*> m_streams;           // this thread
    std::deque<std::unique_ptr<TDiskIOTask>> m_idleTasks;   // this thread
    QMutex m_pendingIdleTasksMutex;
    std::vector<std::unique_ptr<TDiskIOTask>> m_pendingIdleTasks;
    std::atomic<bool> m_idleWorkAllowed{true};
    std::atomic<int> m_idleLimit{1};

    void stop_disk_thread();
    // Between rounds: streams added and removed, new idle tasks
    void take_requests();
    // One round; true when the next one follows without waiting for the tick
    bool do_round();
    // One step of a stream; fill: what it had when the round looked at it.
    // Returns the fill to show for it (-1: out of sync, nothing to show).
    int stream_step(TBufferedAudioStream& stream, int fill);
    void private_add_to_work(TBufferedAudioStream* bufferedAudioStream);
    void private_remove_from_work(TBufferedAudioStream* bufferedAudioStream);

public slots:
    void set_output_sample_rate(uint outputSampleRate);
    void set_resample_quality(int quality);

signals:
	void seekFinished();
};


#endif

//eof
