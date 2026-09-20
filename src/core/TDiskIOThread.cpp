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
#include "TAudioSource.h"
#include "Debugger.h"
#include "Utils.h"

#include <samplerate.h>

#if defined (Q_OS_LINUX)

#include <unistd.h>
#include <sys/syscall.h>
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

/** \class TDiskIOThread
 *	\brief handles all the read's and write's of AudioSources
 *
 *	Each Sheet class has it's own TDiskIOThread instance (one for Reading one for Writing)
 * 	The TDiskIOThread manages all the AudioSources related to a Sheet, and makes sure the RingBuffers
 * 	from the AudioSources are processed in time. (It at least tries very hard)
 */


void TDiskIOThread::run()
{
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
            printf("DiskIOThread: Using prioritized disk I/O using %s prio %d (Only effective with the cfq scheduler)\n", to_prio[ioprio_class], ioprio);
        }
    }
#endif

    while(do_work()) {

    }

    printf("DiskIO::run(): bye\n");

}



TDiskIOThread::TDiskIOThread()
{
    m_audioThreadProcessedFramesQueue = std::make_unique<moodycamel::BlockingReaderWriterCircularBuffer<nframes_t>>(64);
    m_audioSourcesToBeAdded = std::make_unique<moodycamel::BlockingReaderWriterCircularBuffer<TAudioSource*>>(512);
    m_audioSourcesToBeRemoved = std::make_unique<moodycamel::BlockingReaderWriterCircularBuffer<TAudioSource*>>(512);

    m_fileDecodeBuffer = std::make_shared<TFileDecodeBuffer>();
    m_resampleDecodeBuffer = std::make_shared<TFileDecodeBuffer>();

    m_seekRequested.store(false);
    m_stopDiskIOThreadRequested = false;
    m_outputSampleRate = 0;
    m_sampleRateChanged = false;
    m_resampleQualityChanged = false;
    m_resampleQuality = SRC_SINC_FASTEST;
    m_bufferFillStatus = 0;
    m_doWorktTime.store(0);
    m_lastCpuReadTime = TTimeRef::get_nanoseconds_since_epoch();

    moveToThread(this);
    start(QThread::HighPriority);
}


TDiskIOThread::~TDiskIOThread()
{
    PENTERDES;
    stop_disk_thread();

    // the shared buffers should have only one user by now, that is us, so only 1 user
    Q_ASSERT(m_fileDecodeBuffer.use_count() == 1);
    Q_ASSERT(m_resampleDecodeBuffer.use_count() == 1);
}



/**
* 	Seek's all the ReadSources readbuffers to the new position.
*	Call prepare_seek() first, to interupt do_work() if it was running.
* 
*  N.B. this function resets the ReadSource buffers assuming it is the only thread
*  accessing the buffers. If the audio thread is accessing the buffers at this point
*  the integrity of the buffers cannot be garuanteed!
*/
void TDiskIOThread::seek()
{
    PENTER;

    Q_ASSERT_X(this->thread() == QThread::currentThread(), "DiskIO::seek", "NOT running in DiskIO thread");
    Q_ASSERT(m_seekRequested.load() == true);

    printf("DiskIO::seek: Seeking to %s\n", QS_C(TTimeRef::timeref_to_ms_3(m_seekTransportLocation)));

    // A seek event happens for 2 reasons, for transport control and after an audiodevice reconfiguration
    // in the latter case we need to reset rate and buffer sizes.
    if (m_sampleRateChanged) {
        nframes_t bufferSize = audiodevice().get_buffer_size();
        for (auto source : std::as_const(m_audioSources)) {
            source->set_output_rate_and_convertor_type(m_outputSampleRate, m_resampleQuality);
            source->prepare_rt_buffers(bufferSize);
        }
        m_sampleRateChanged = false;
        m_fileDecodeBuffer->check_buffers_capacity(bufferSize, 2);
    }

    for(auto source : std::as_const(m_audioSources)) {
        source->rb_seek_to_transport_location(m_seekTransportLocation);
    }

    m_transportLocation = m_seekTransportLocation;
    m_seekRequested.store(false);

    emit seekFinished();
}


bool TDiskIOThread::do_work( )
{
    Q_ASSERT_X(this->thread() == QThread::currentThread(), "DiskIO::do_work", "NOT running in DiskIO thread");

    if (m_stopDiskIOThreadRequested) {
        return false;
    }

    nframes_t audioThreadProcessedFrames;
    // Here we wait (block) till at least one of the following has happened:
    // 1: the owner of this DiskIOThread has put at least 1 value via add_processed_audio_thread_frames(nframes_t nframes)
    // usually before or after a process cycle
    // 2: wake_up() has been called by the owner of this object which essentially does the same thing
    m_audioThreadProcessedFramesQueue->wait_dequeue(audioThreadProcessedFrames);

    auto startTime = TTimeRef::get_nanoseconds_since_epoch();

    nframes_t totalFrames = audioThreadProcessedFrames;

    while(m_audioThreadProcessedFramesQueue->try_dequeue(audioThreadProcessedFrames)) {
        totalFrames += audioThreadProcessedFrames;
    }
    Q_UNUSED(totalFrames);

    TAudioSource* source;
    while (m_audioSourcesToBeAdded->try_dequeue(source)) {
        private_add_to_work(source);
    }
    while (m_audioSourcesToBeRemoved->try_dequeue(source)) {
        private_remove_from_work(source);
    }

    if (m_resampleQualityChanged) {
        for (auto source : std::as_const(m_audioSources)) {
            source->set_output_rate_and_convertor_type(m_outputSampleRate, m_resampleQuality);
        }
        m_resampleQualityChanged = false;
    }

    check_for_seek_requested();

    for (const auto source : std::as_const(m_audioSources))
    {
        TAudioSourceBufferStatus* status = source->get_buffer_status();

        if (status->get_fill_status() <= 80 || status->out_of_sync()) {

            if (status->out_of_sync()) {
                source->rb_seek_to_transport_location(m_transportLocation);
            }
            else {
                source->process_realtime_buffers();
            }

            if ((status->get_fill_status() < m_bufferFillStatus.load()) && !status->out_of_sync()) {
                m_bufferFillStatus.store(status->get_fill_status());
            }
        }

        check_for_seek_requested();
    }

    auto totalTime = TTimeRef::get_nanoseconds_since_epoch() - startTime;
    m_doWorktTime.fetch_add(totalTime);

    return true;
}

void TDiskIOThread::check_for_seek_requested()
{
    if (m_seekRequested.load()) {
        printf("DiskIO::do_work: Seek requested, starting seek now\n");
        seek();
    }
}

void TDiskIOThread::add_audio_source(TAudioSource* source)
{
    PENTER2;

    Q_ASSERT(source);
    if (source->get_channel_count() == 0) {
        PMESG("TDiskIOThread::add_audio_source: source has no channels, not adding it to queue");
        return;
    }

    source->set_output_rate_and_convertor_type(m_outputSampleRate, m_resampleQuality);
    source->set_file_decode_buffer(m_fileDecodeBuffer);
    source->set_resample_decode_buffer(m_resampleDecodeBuffer);

    source->prepare_rt_buffers(audiodevice().get_buffer_size());

    m_audioSourcesToBeAdded->wait_enqueue(source);
}

void TDiskIOThread::private_add_to_work(TAudioSource *source)
{
    PENTER2;
    Q_ASSERT(this->thread() == QThread::currentThread());
    Q_ASSERT(!m_audioSources.contains(source));

    m_audioSources.append(source);
}

void TDiskIOThread::remove_audio_source(TAudioSource *source)
{
    PENTER2;

    m_audioSourcesToBeRemoved->wait_enqueue(source);
}

void TDiskIOThread::private_remove_from_work(TAudioSource *source)
{
    PENTER2;

    Q_ASSERT(this->thread() == QThread::currentThread());
    m_audioSources.removeAll(source);

    // FIXME
    // Review the deletion of AudioSources and non-active AudioSources that should only
    // be removed from DiskIO but not deleted.
    source->m_bufferstatus.set_sync_status(TAudioSourceBufferStatus::QUEUE_ABOUT_TO_BE_DELETED);
    source->delete_queue_buffers();
    delete source;
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

    // Stop any processing in do_work()
    m_stopDiskIOThreadRequested = true;
    // this function is called from the DiskIO destructor
    // most likely we're waiting on an empty blocking queue so do_work() will never be called
    // so make sure we wake up the thread by adding an item to the queue
    // Since we are disconnected from the audio processing callback it's safe to do so.
    wakeup();
    quit();
    wakeup();
    if (!wait(500)) {
        terminate();
        wait(500);
    }
}

void TDiskIOThread::set_resample_quality(int quality)
{
    m_resampleQuality = quality;
    m_resampleQualityChanged = true;
}

void TDiskIOThread::set_output_sample_rate(uint outputSampleRate)
{
    printf("DiskIO::set_output_sample_rate: new sample rate %d\n", outputSampleRate);
    m_outputSampleRate = outputSampleRate;
    m_sampleRateChanged = true;
}

