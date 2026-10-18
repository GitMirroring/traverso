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

#include <QList>
#include <QMutex>
#include <QThread>

#include <chrono>
#include <deque>
#include <functional>

#include "TTimeRef.h"
#include "defines.h"

#include "cameron/readerwritercircularbuffer.h"

class TBufferedAudioStream;

class TDiskIOThread : public QThread
{
	Q_OBJECT

public:
    TDiskIOThread();
    ~TDiskIOThread();
	
    void set_transport_location(const TTimeRef& transportLocation) {
        m_transportLocation = transportLocation;
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

    // Background work for the idle time: when no stream needs data, the
    // thread spends at most IdleBudget per wakeup on these, one after the
    // other. A task returns true while it has more to do. Any thread. Adding
    // one does not wake the thread: the audio thread wakes it every cycle,
    // also with the transport stopped (a failed driver falls back to the
    // Dummy driver, which has cycles too).
    using IdleTask = std::function<bool(std::chrono::nanoseconds budget)>;
    void add_idle_task(IdleTask task);

    // false while the sheet records: the disk belongs to the recording. Per
    // sheet: the other sheets go on (peak files are rebuilt only after a new
    // file version or when they are lost, and nobody records then)
    void set_idle_work_allowed(bool allowed) { m_idleWorkAllowed.store(allowed, std::memory_order_relaxed); }

    static constexpr std::chrono::milliseconds IdleBudget{3};

protected:
    void run() override;

private:
    std::unique_ptr<moodycamel::BlockingReaderWriterCircularBuffer<nframes_t>>      m_audioThreadProcessedFramesQueue;
    std::unique_ptr<moodycamel::BlockingReaderWriterCircularBuffer<TBufferedAudioStream*>>   m_streamsToBeAdded;
    std::unique_ptr<moodycamel::BlockingReaderWriterCircularBuffer<TBufferedAudioStream*>>   m_streamsToBeRemoved;

    std::atomic<bool>   m_seekRequested;
    std::atomic<bool>   m_stopDiskIOThreadRequested;

    QList<TBufferedAudioStream*>	m_bufferedAudioStreams;

    std::atomic<int>    m_bufferFillStatus;
    std::atomic<trav_time_t> m_doWorktTime;
    trav_time_t         m_lastCpuReadTime;

    int                 m_resampleQuality;
    bool                m_resampleQualityChanged;
    bool                m_sampleRateChanged;

    uint                m_outputSampleRate{};

    TTimeRef            m_transportLocation;
    TTimeRef            m_seekTransportLocation;
	
    void stop_disk_thread();
    // Runs idle tasks when no stream needs reading; true while tasks remain
    void run_idle_tasks_when_idle();
    bool run_idle_tasks(std::chrono::nanoseconds budget);

    QMutex                  m_idleTasksMutex;
    std::deque<IdleTask>    m_idleTasks;
    std::atomic<bool>       m_idleWorkAllowed{true};
    bool                    m_moreIdleWork{false};   // this thread only

public slots:
    void set_output_sample_rate(uint outputSampleRate);
    void set_resample_quality(int quality);

private slots:
    bool do_work();

    void private_add_to_work(TBufferedAudioStream* bufferedAudioStream);
    void private_remove_from_work(TBufferedAudioStream* bufferedAudioStream);

signals:
	void seekFinished();
};


#endif

//eof
