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

#include "DiskIO.h"

#include "AudioDevice.h"

#include "AudioSource.h"



#include "Debugger.h"
#include "TFileDecodeBuffer.h"
#include "Utils.h"
#include <samplerate.h>

#if defined (Q_OS_UNIX)

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

#endif // endif Q_OS_UNIX

/** \class DiskIO
 *	\brief handles all the read's and write's of AudioSources in it's private thread.
 *
 *	Each Sheet class has it's own DiskIO instance.
 * 	The DiskIO manages all the AudioSources related to a Sheet, and makes sure the RingBuffers
 * 	from the AudioSources are processed in time. (It at least tries very hard)
 */


void DiskIO::run()
{
#if defined (Q_OS_UNIX)

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



DiskIO::DiskIO()
{
    m_audioThreadProcessedFramesQueue = new moodycamel::BlockingReaderWriterCircularBuffer<nframes_t>(64);
    m_audioSourcesToBeAdded = new moodycamel::BlockingReaderWriterCircularBuffer<AudioSource*>(512);
    m_audioSourcesToBeRemoved = new moodycamel::BlockingReaderWriterCircularBuffer<AudioSource*>(512);

    m_seekRequested.store(false);
    m_stopDiskIOThreadRequested = false;
    m_outputSampleRate = 0;
    m_sampleRateChanged = false;
    m_resampleQualityChanged = false;
    m_resampleQuality = SRC_SINC_FASTEST;
    m_bufferFillStatus = 0;
    m_doWorktTime.store(0);
    m_lastCpuReadTime = TTimeRef::get_nanoseconds_since_epoch();

    // TODO This is a LARGE buffer, any ideas how to make it smaller ??
    // FIXME: this buffer is never resized and an ugly hack so fix it!
    framebuffer = new audio_sample_t[audiodevice().get_sample_rate() * writebuffertime];

    m_fileDecodeBuffer = new TFileDecodeBuffer;
    m_resampleDecodeBuffer = new TFileDecodeBuffer;

    moveToThread(this);
    start(QThread::HighPriority);
}


DiskIO::~DiskIO()
{
    PENTERDES;
    stop_disk_thread();

    delete m_audioThreadProcessedFramesQueue;
    delete m_audioSourcesToBeAdded;
    delete m_audioSourcesToBeRemoved;

    delete [] framebuffer;
    delete m_fileDecodeBuffer;
    delete m_resampleDecodeBuffer;
}



/**
* 	Seek's all the ReadSources readbuffers to the new position.
*	Call prepare_seek() first, to interupt do_work() if it was running.
* 
*  N.B. this function resets the ReadSource buffers assuming it is the only thread
*  accessing the buffers. If the audio thread is accessing the buffers at this point
*  the integrity of the buffers cannot be garuanteed!
*/
void DiskIO::seek()
{
    PENTER;

    Q_ASSERT_X(this->thread() == QThread::currentThread(), "DiskIO::seek", "NOT running in DiskIO thread");
    Q_ASSERT(m_seekRequested.load() == true);

    printf("DiskIO::seek: Seeking to %s\n", QS_C(TTimeRef::timeref_to_ms_3(m_seekTransportLocation)));

    // A seek event happens for 2 reasons, for transport control and after an audiodevice reconfiguration
    // in the latter case we need to reset rate and buffer sizes.
    if (m_sampleRateChanged) {
        for (auto source : m_audioSources) {
            source->set_output_rate_and_convertor_type(m_outputSampleRate, m_resampleQuality);
            source->prepare_rt_buffers(audiodevice().get_buffer_size());
        }
        m_sampleRateChanged = false;
    }

    for(auto source : m_audioSources) {
        source->rb_seek_to_transport_location(m_seekTransportLocation);
    }

    m_transportLocation = m_seekTransportLocation;
    m_seekRequested.store(false);

    emit seekFinished();
}


bool DiskIO::do_work( )
{
    Q_ASSERT_X(this->thread() == QThread::currentThread(), "DiskIO::do_work", "NOT running in DiskIO thread");

    if (m_stopDiskIOThreadRequested) {
        return false;
    }

    nframes_t audioThreadProcessedFrames;
    m_audioThreadProcessedFramesQueue->wait_dequeue(audioThreadProcessedFrames);

    nframes_t totalFrames = audioThreadProcessedFrames;
    while(m_audioThreadProcessedFramesQueue->try_dequeue(audioThreadProcessedFrames)) {
        totalFrames += audioThreadProcessedFrames;
    }
    Q_UNUSED(totalFrames);

    AudioSource* source;
    while (m_audioSourcesToBeAdded->try_dequeue(source)) {
        private_add_to_work(source);
    }
    while (m_audioSourcesToBeRemoved->try_dequeue(source)) {
        private_remove_from_work(source);
    }

    auto startTime = TTimeRef::get_nanoseconds_since_epoch();

    if (m_resampleQualityChanged) {
        for (auto source : m_audioSources) {
            source->set_output_rate_and_convertor_type(m_outputSampleRate, m_resampleQuality);
        }
        m_resampleQualityChanged = false;
    }


    for (auto source : m_audioSources)
    {
        if (m_seekRequested.load()) {
            printf("DiskIO::do_work: Seek requested, starting seek now\n");
            seek();
        }

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
    }

    auto totalTime = TTimeRef::get_nanoseconds_since_epoch() - startTime;
    m_doWorktTime.fetch_add(totalTime);

    return true;
}

void DiskIO::add_audio_source(AudioSource* source)
{
    PENTER2;

    Q_ASSERT(source->get_channel_count() > 0);
    Q_ASSERT(source);

    source->set_output_rate_and_convertor_type(m_outputSampleRate, m_resampleQuality);
    source->set_decode_buffers(m_fileDecodeBuffer, m_resampleDecodeBuffer);

    source->prepare_rt_buffers(audiodevice().get_buffer_size());

    // only for WriteSource change to decodebuffers instead
    source->set_diskio_frame_buffer(framebuffer);

    m_audioSourcesToBeAdded->wait_enqueue(source);
}

void DiskIO::private_add_to_work(AudioSource *source)
{
    PENTER2;
    Q_ASSERT(this->thread() == QThread::currentThread());
    Q_ASSERT(!m_audioSources.contains(source));

    m_audioSources.append(source);
}

void DiskIO::remove_audio_source(AudioSource *source)
{
    PENTER2;

    m_audioSourcesToBeRemoved->wait_enqueue(source);
}

void DiskIO::private_remove_from_work(AudioSource *source)
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
 * @return Returns the CPU time consumed by the DiskIO thread
 */
bool DiskIO::get_cpu_time(float &time)
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
int DiskIO::get_buffers_fill_status( )
{
    int status = m_bufferFillStatus.load();
    m_bufferFillStatus.store(100);

    return status;
}

void DiskIO::add_processed_audio_thread_frames(nframes_t nframes)
{
    m_audioThreadProcessedFramesQueue->try_enqueue(nframes);
}

void DiskIO::wakeup()
{
    m_audioThreadProcessedFramesQueue->try_enqueue(0);
}

void DiskIO::stop_disk_thread( )
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

void DiskIO::set_resample_quality(int quality)
{
    m_resampleQuality = quality;
    m_resampleQualityChanged = true;
}

void DiskIO::set_output_sample_rate(uint outputSampleRate)
{
    printf("DiskIO::set_output_sample_rate: new sample rate %d\n", outputSampleRate);
    m_outputSampleRate = outputSampleRate;
    m_sampleRateChanged = true;
}

