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

#include "TDiskIOThread.h"

#include "TAudioDevice.h"
#include "TBufferedAudioStream.h"
#include "Debugger.h"

#include <QFile>
#include <QFileInfo>
#include <QtConcurrent>
#include <QThreadPool>

#include <algorithm>

#include <samplerate.h>

#if defined (Q_OS_LINUX)

#include <unistd.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <sys/types.h>

#if defined(__i386__)
# define __NR_ioprio_set	289
# define __NR_ioprio_get	290
# define IOPRIO_SUPPORT		1
#elif defined(__ppc__) || defined(__powerpc__) || defined(__PPC__)
# define __NR_ioprio_set	273
# define __NR_ioprio_get	274
# define IOPRIO_SUPPORT		1
#elif defined(__x86_64__)
# define __NR_ioprio_set	251
# define __NR_ioprio_get	252
# define IOPRIO_SUPPORT		1
#elif defined(__ia64__)
# define __NR_ioprio_set	1274
# define __NR_ioprio_get	1275
# define IOPRIO_SUPPORT		1
#else
# define IOPRIO_SUPPORT		0
#endif

enum {
    IOPRIO_CLASS_NONE,
    IOPRIO_CLASS_RT,
    IOPRIO_CLASS_BE,
    IOPRIO_CLASS_IDLE,
};

enum {
    IOPRIO_WHO_PROCESS = 1,
    IOPRIO_WHO_PGRP,
    IOPRIO_WHO_USER,
};

const char *to_prio[] = { "none", "realtime", "best-effort", "idle", };
#define IOPRIO_CLASS_SHIFT	13
#define IOPRIO_PRIO_MASK	0xff

#endif // endif Q_OS_LINUX

// A stream as a task of a round: what it told the round (whether it needs
// work), and its step, which reads (or writes) all it can at once
class TDiskIOThread::StreamTask final : public TDiskIOTask
{
public:
    StreamTask(TDiskIOThread& diskio, TBufferedAudioStream* stream) : m_diskio(diskio), m_stream(stream) {}

    // The stream's state as the current round looked at it (do_round())
    void set_state(const TDiskIOPlanner::Stream& state) { m_state = state; }
    const TDiskIOPlanner::Stream& state() const { return m_state; }

    bool needs_work() const override { return m_state.needsWork; }
    void step() override { m_diskio.stream_step(*this); }

    TBufferedAudioStream* stream() const { return m_stream; }

private:
    TDiskIOThread& m_diskio;
    TBufferedAudioStream* m_stream;
    TDiskIOPlanner::Stream m_state;
};


void TDiskIOThread::run()
{
    TLog::name_thread("diskio");
#if defined(Q_OS_LINUX)
    // struct sched_param param;
    // param.sched_priority = 40;
    // if (pthread_setschedparam (pthread_self(), SCHED_FIFO, &param) != 0) {}

    if (IOPRIO_SUPPORT) {
        // When using the cfq scheduler we are able to set the priority of the io for what it's worth though :-)
        int ioprio = 0, ioprio_class = IOPRIO_CLASS_RT;
        int value = syscall(__NR_ioprio_set, IOPRIO_WHO_PROCESS, getpid(), ioprio | ioprio_class << IOPRIO_CLASS_SHIFT);

        if (value == -1) {
            ioprio_class = IOPRIO_CLASS_BE;
            value = syscall(__NR_ioprio_set, IOPRIO_WHO_PROCESS, getpid(), ioprio | ioprio_class << IOPRIO_CLASS_SHIFT);
        }

        if (value == 0) {
            ioprio = syscall (__NR_ioprio_get, IOPRIO_WHO_PROCESS, getpid());
            ioprio_class = ioprio >> IOPRIO_CLASS_SHIFT;
            ioprio = ioprio & IOPRIO_PRIO_MASK;
            TLOG_I("diskio", "prioritized disk I/O: {} prio {} (only effective with the cfq scheduler)", to_prio[ioprio_class], ioprio);
        }
    }
#endif

    bool moreWork = false;
    while (true) {
        // The tick: the audio thread wakes this thread every cycle (also
        // with the transport stopped). After a round with idle work the next
        // one follows at once; the wakeups that came meanwhile are taken.
        nframes_t frames;
        if (!moreWork) {
            m_audioThreadProcessedFramesQueue->wait_dequeue(frames);
        }
        while (m_audioThreadProcessedFramesQueue->try_dequeue(frames)) {
        }

        take_requests();
        if (m_stopDiskIOThreadRequested.load()) {
            break;
        }

        const trav_time_t startTime = TTimeRef::get_nanoseconds_since_epoch();
        moreWork = do_round();
        m_doWorktTime.fetch_add(TTimeRef::get_nanoseconds_since_epoch() - startTime);
    }

    TLOG_I("diskio", "stopped");
}



TDiskIOThread::TDiskIOThread()
{
    m_audioThreadProcessedFramesQueue = std::make_unique<moodycamel::BlockingReaderWriterCircularBuffer<nframes_t>>(64);
    m_streamsToBeAdded = std::make_unique<moodycamel::BlockingReaderWriterCircularBuffer<TBufferedAudioStream*>>(512);
    m_streamsToBeRemoved = std::make_unique<moodycamel::BlockingReaderWriterCircularBuffer<TBufferedAudioStream*>>(512);

    m_seekRequested.store(false);
    m_stopDiskIOThreadRequested.store(false);
    m_outputSampleRate = 0;
    m_sampleRateChanged = false;
    m_resampleQualityChanged = false;
    m_resampleQuality = SRC_SINC_FASTEST;
    m_bufferFillStatus = 0;
    m_doWorktTime.store(0);
    m_lastCpuReadTime = TTimeRef::get_nanoseconds_since_epoch();

    moveToThread(this);
    start();

    int maxThreads = QThread::idealThreadCount() / 2;
    QThreadPool::globalInstance()->setMaxThreadCount(maxThreads > 0 ? maxThreads : 1);
}

TDiskIOThread::~TDiskIOThread()
{
    PENTERDES;
    stop_disk_thread();

    // Background tasks that did not get to their end are handed back too
    // (the thread is gone: this one may touch its lists)
    QMutexLocker locker(&m_pendingIdleTasksMutex);
    for (const std::unique_ptr<TDiskIOTask>& task : m_pendingIdleTasks) {
        task->retire();
    }
    for (const std::unique_ptr<TDiskIOTask>& task : m_idleTasks) {
        task->retire();
    }
}

void TDiskIOThread::take_requests()
{
    TBufferedAudioStream* stream;
    while (m_streamsToBeAdded->try_dequeue(stream)) {
        private_add_to_work(stream);
    }
    while (m_streamsToBeRemoved->try_dequeue(stream)) {
        private_remove_from_work(stream);
    }

    QMutexLocker locker(&m_pendingIdleTasksMutex);
    for (std::unique_ptr<TDiskIOTask>& task : m_pendingIdleTasks) {
        m_idleTasks.push_back(std::move(task));
    }
    m_pendingIdleTasks.clear();
}

bool TDiskIOThread::do_round()
{
    Q_ASSERT_X(this->thread() == QThread::currentThread(), "DiskIO::do_round", "NOT running in DiskIO thread");

    if (m_resampleQualityChanged) {
        for (const std::unique_ptr<StreamTask>& task : m_streams) {
            task->stream()->set_output_rate_and_convertor_type(m_outputSampleRate, m_resampleQuality);
        }
        m_resampleQualityChanged = false;
    }

    const bool seek = m_seekRequested.load();
    m_round.seek = seek;
    m_round.seekLocation = m_seekTransportLocation;
    m_round.transportLocation = TTimeRef(m_transportLocation.load(std::memory_order_relaxed));
    m_round.sampleRateChanged = m_sampleRateChanged;
    m_round.outputRate = m_outputSampleRate;
    m_round.resampleQuality = m_resampleQuality;
    m_round.bufferSize = seek ? audiodevice().get_buffer_size() : 0;
    if (seek) {
        TLOG_D("diskio", "seek requested, seeking");
    }

    // What every stream needs, the idle tasks that have work
    std::vector<TDiskIOPlanner::Stream> streams;
    streams.reserve(m_streams.size());
    for (const std::unique_ptr<StreamTask>& task : m_streams) {
        task->set_state(task->stream()->disk_state(m_round.transportLocation));
        streams.push_back(task->state());
    }
    std::vector<TDiskIOTask*> idle;
    for (const std::unique_ptr<TDiskIOTask>& task : m_idleTasks) {
        if (task->needs_work()) {
            idle.push_back(task.get());
        }
    }

    // blockingMap() starts the round on the pool and waits: the pool's
    // threads do the work, this thread takes part only when the pool is busy
    // (it runs the round then alone, so DiskIO always gets on)
    m_planner.set_threads(QThreadPool::globalInstance()->maxThreadCount());
    m_planner.set_idle_limit(m_idleLimit.load(std::memory_order_relaxed));
    const TDiskIOPlanner::Round plan = m_planner.plan(streams, seek, int(idle.size()),
                                                      m_idleWorkAllowed.load(std::memory_order_relaxed));

    // Idle steps that have not started yet give way to a seek that comes in
    // meanwhile (they get their step in a later round); a read that runs
    // already finishes first
    struct Step
    {
        TDiskIOTask* task;
        bool idle;
    };
    std::vector<Step> round;
    round.reserve(plan.streams.size() + size_t(plan.idleSteps));
    for (const int index : plan.streams) {
        round.push_back(Step{m_streams[size_t(index)].get(), false});
    }
    for (int i = 0; i < plan.idleSteps; ++i) {
        round.push_back(Step{idle[size_t(i)], true});
    }
    auto run = [this](const Step& step) {
        if (!(step.idle && m_seekRequested.load(std::memory_order_relaxed))) {
            step.task->step();
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
        QtConcurrent::blockingMap(round, [this, &run](const Step& step) {
            m_roundStarted.load(std::memory_order_acquire);
            run(step);
            m_stepsDone.fetch_add(1, std::memory_order_release);
        });
        m_stepsDone.load(std::memory_order_acquire);
    }

    if (seek) {
        m_sampleRateChanged = false;
        m_transportLocation.store(m_seekTransportLocation.universal_frame(), std::memory_order_relaxed);
        m_seekRequested.store(false);
        emit seekFinished();
    }

    // Idle tasks that are over go (a build reports its result then)
    for (auto it = m_idleTasks.begin(); it != m_idleTasks.end();) {
        if ((*it)->is_done()) {
            (*it)->retire();
            it = m_idleTasks.erase(it);
        } else {
            ++it;
        }
    }

    return plan.continueWithoutTick;
}

void TDiskIOThread::report_fill(int fill)
{
    int lowest = m_bufferFillStatus.load(std::memory_order_relaxed);
    while (fill < lowest && !m_bufferFillStatus.compare_exchange_weak(lowest, fill, std::memory_order_relaxed)) {
    }
}

void TDiskIOThread::stream_step(StreamTask& task)
{
    static thread_local TFileIOBuffer threadLocalFileDecodeBuffer;
    const Round& round = m_round;
    TBufferedAudioStream* stream = task.stream();

    if (round.seek) {
        if (round.sampleRateChanged) {
            stream->set_output_rate_and_convertor_type(round.outputRate, round.resampleQuality);
            stream->prepare_rt_buffers(round.bufferSize);
        }
        stream->rb_seek_to_transport_location(threadLocalFileDecodeBuffer, round.seekLocation);
        // The seek fills part of the ring; the rest at once
        TBufferedAudioStreamStatus& status = stream->get_buffer_status();
        const int fill = status.get_fill_status();
        if (!status.out_of_sync() && fill <= TDiskIOPlanner::IdleStartThreshold) {
            stream->process_realtime_buffers(threadLocalFileDecodeBuffer);
        }
        if (!status.out_of_sync()) {
            report_fill(fill);
        }
        return;
    }

    // Out of sync: seek to where the transport is (a reader whose window the
    // transport entered); otherwise read or write all there is room for
    if (stream->get_buffer_status().out_of_sync()) {
        stream->rb_seek_to_transport_location(threadLocalFileDecodeBuffer, round.transportLocation);
    } else {
        stream->process_realtime_buffers(threadLocalFileDecodeBuffer);
    }

    // For the GUI: the emptiest stream in sync, with the fill it had when
    // the round looked at it (its low point, before this step refilled or
    // flushed it)
    if (!stream->m_bufferstatus.out_of_sync()) {
        report_fill(task.state().fill);
    }
}

int TDiskIOThread::idle_limit_for(const QString& directory)
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

void TDiskIOThread::add_idle_task(std::unique_ptr<TDiskIOTask> task)
{
    QMutexLocker locker(&m_pendingIdleTasksMutex);
    m_pendingIdleTasks.push_back(std::move(task));
}

void TDiskIOThread::add_buffered_audio_stream(TBufferedAudioStream* bufferedAudioStream)
{
    PENTER2;

    Q_ASSERT(bufferedAudioStream);
    if (bufferedAudioStream->get_channel_count() == 0) {
        PMESG("TDiskIOThread::add_buffered_audio_stream: Buffered Audio Stream has no channels, not adding it to queue");
        return;
    }

    bufferedAudioStream->set_output_rate_and_convertor_type(m_outputSampleRate, m_resampleQuality);
    bufferedAudioStream->prepare_rt_buffers(audiodevice().get_buffer_size());

    m_streamsToBeAdded->wait_enqueue(bufferedAudioStream);
}

void TDiskIOThread::private_add_to_work(TBufferedAudioStream *bufferedAudioStream)
{
    PENTER2;
    Q_ASSERT(this->thread() == QThread::currentThread());
    Q_ASSERT(std::none_of(m_streams.begin(), m_streams.end(), [bufferedAudioStream](const std::unique_ptr<StreamTask>& task) {
        return task->stream() == bufferedAudioStream;
    }));

    m_streams.push_back(std::make_unique<StreamTask>(*this, bufferedAudioStream));
}

void TDiskIOThread::remove_buffered_audio_stream(TBufferedAudioStream *bufferedAudioStream)
{
    PENTER2;

    m_streamsToBeRemoved->wait_enqueue(bufferedAudioStream);
}

void TDiskIOThread::private_remove_from_work(TBufferedAudioStream *bufferedAudioStream)
{
    PENTER2;

    Q_ASSERT(this->thread() == QThread::currentThread());
    std::erase_if(m_streams, [bufferedAudioStream](const std::unique_ptr<StreamTask>& task) {
        return task->stream() == bufferedAudioStream;
    });

    // FIXME
    // Review the deletion of TBufferedAudioStreams and non-active TBufferedAudioStreams that should only
    // be removed from DiskIO but not deleted.
    bufferedAudioStream->m_bufferstatus.set_sync_status(TBufferedAudioStreamStatus::QUEUE_ABOUT_TO_BE_DELETED);
    bufferedAudioStream->delete_queue_buffers();
    delete bufferedAudioStream;
}

/**
 *
 * @return Returns the CPU time consumed by the TDiskIOThread thread
 */
bool TDiskIOThread::get_cpu_time(float &time)
{
    trav_time_t currentTime = TTimeRef::get_nanoseconds_since_epoch();

    time = (m_doWorktTime.load()  / float(currentTime - m_lastCpuReadTime)) * 100;

    m_doWorktTime.store(0);
    m_lastCpuReadTime = currentTime;

    return true;
}


/**
 * 	Get the status of the writebuffers.
 *
 * @return The status in procentual amount of the smallest remaining space in the writebuffers
 *		that could be used to write 'recording' data too.
 */
int TDiskIOThread::get_buffers_fill_status( )
{
    int status = m_bufferFillStatus.load();
    m_bufferFillStatus.store(100);

    return status;
}

void TDiskIOThread::stop_disk_thread( )
{
    PENTER;

    // Stop any processing in the round loop
    m_stopDiskIOThreadRequested.store(true);

    // this function is called from the DiskIO destructor
    // most likely we're waiting on an empty blocking queue so the loop will never go on
    // so make sure we wake up the thread by adding an item to the queue
    // Since we are disconnected from the audio processing callback it's safe to do so.
    wakeup(); // make the loop leave TDiskIOThread::run()

    // Until the running round is over: its steps run on pool threads and use
    // the tasks this object owns. A round always ends (every step is one
    // read or write); killing the thread would leave the steps running on
    // deleted tasks.
    wait();
}

void TDiskIOThread::set_resample_quality(int quality)
{
    m_resampleQuality = quality;
    m_resampleQualityChanged = true;
}

void TDiskIOThread::set_output_sample_rate(uint outputSampleRate)
{
    TLOG_I("diskio", "output sample rate {}", outputSampleRate);
    m_outputSampleRate = outputSampleRate;
    m_sampleRateChanged = true;
}

