/*
Copyright (C) 2006-2007 Remon Sijrier

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
#include "ResampleAudioReader.h"
#include "Sheet.h"
#include "Utils.h"

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

#include "AbstractAudioReader.h"
#include "AudioSource.h"
#include "ReadSource.h"
#include "WriteSource.h"
#include "AudioDevice.h"
#include "TConfig.h"

// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"


#define UPDATE_INTERVAL		40

DiskIOThread::DiskIOThread()
    : QThread()
{
}

void DiskIOThread::run()
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
    exec();
}


/************** END DISKIO THREAD ************/



/** 	\class DiskIO 
 *	\brief handles all the read's and write's of AudioSources in it's private thread.
 *
 *	Each Sheet class has it's own DiskIO instance.
 * 	The DiskIO manages all the AudioSources related to a Sheet, and makes sure the RingBuffers
 * 	from the AudioSources are processed in time. (It at least tries very hard)
 */
DiskIO::DiskIO(Sheet* sheet)
    : m_sheet(sheet)
{
    m_lastdoWorkReadTime = TTimeRef::get_nanoseconds_since_epoch();
    m_stopWork.store(false);
    m_seeking.store(false);
    m_sampleRateChanged = false;
    m_resampleQuality = config().get_property("Conversion", "RTResamplingConverterType", ResampleAudioReader::get_default_resample_quality()).toInt();
    m_readBufferFillStatus = m_writeBufferFillStatus = 0;
    m_hardDiskOverLoadCounter = 0;
    m_cpuTime = new RingBufferNPT<trav_time_t>(1024);

    // TODO This is a LARGE buffer, any ideas how to make it smaller ??
    framebuffer = new audio_sample_t[audiodevice().get_sample_rate() * writebuffertime];

    m_fileDecodeBuffer = new DecodeBuffer;
    m_resampleDecodeBuffer = new DecodeBuffer;

    // Move this instance to the workthread
    moveToThread(&m_diskThread);
    m_diskThread.start(QThread::TimeCriticalPriority);
    connect(&audiodevice(), SIGNAL(finishedOneProcessCycle()), this, SLOT(do_work()), Qt::QueuedConnection);
}


DiskIO::~DiskIO()
{
    PENTERDES;
    stop_disk_thread();
    delete framebuffer;
    delete m_fileDecodeBuffer;
    delete m_resampleDecodeBuffer;
    delete m_cpuTime;

}

/**
* 	Seek's all the ReadSources readbuffers to the new position.
*	Call prepare_seek() first, to interupt do_work() if it was running.
* 
* @param position The position to seek too 
*/
void DiskIO::seek()
{
    PENTER;

// #if defined (THREAD_CHECK)
    Q_ASSERT_X(this->thread() == QThread::currentThread(), "DiskIO::seek", "Error, running in gui thread!!!!!");
// #endif

    mutex.lock();

    m_stopWork.store(false);
    m_seeking.store(true);

    TTimeRef transportLocation = m_sheet->get_new_transport_location();

    for(ReadSource* source : m_readSources) {
        if (m_sampleRateChanged) {
            source->set_diskio(this);
        }
        source->rb_seek_to_transport_location(transportLocation);
    }

    m_sampleRateChanged = false;

    mutex.unlock();

    // Now, fill the buffers like normal
    do_work();

    m_readBufferFillStatus.store(0);

    m_seeking.store(false);

    emit seekFinished();
}


void DiskIO::output_rate_changed(uint rate)
{
    m_sampleRateChanged = true;
    m_outputRate = rate;
}


// Internal function
void DiskIO::do_work( )
{
// #if defined (THREAD_CHECK)
    Q_ASSERT_X(this->thread() == QThread::currentThread(), "DiskIO::seek", "Error, running in gui thread!!!!!");
// #endif

    // QMutexLocker locker(&mutex);

    m_hardDiskOverLoadCounter = 0;

    m_doWorkStartTime = TTimeRef::get_nanoseconds_since_epoch();

    there_are_processable_sources();

    for (int i=0; i<m_processableReadSources.size(); ++i) {
        ReadSource* source = m_processableReadSources.at(i);

        if (m_stopWork.load()) {
            printf("DiskIO::do_work: Detected stop work, returning from do_work()\n");
            update_time_usage(TTimeRef::get_nanoseconds_since_epoch());
            return;
        }

        if (source->get_buffer_status()->out_of_sync()) {
            source->rb_seek_to_transport_location(m_sheet->get_transport_location());
            source->fill_realtime_buffers(true);
        }
        else {
            source->fill_realtime_buffers(m_seeking.load());
        }

    }

    for (int i=0; i<m_processableWriteSources.size(); ++i) {
        WriteSource* source = m_processableWriteSources.at(i);
        source->process_ringbuffer(framebuffer);
    }

    update_time_usage(TTimeRef::get_nanoseconds_since_epoch());

}


// Internal function
int DiskIO::there_are_processable_sources( )
{
    m_processableReadSources.clear();
    m_processableSyncSources.clear();
    m_processableWriteSources.clear();
    m_writersStatus.clear();


    for (int j=0; j<m_writeSources.size(); ++j) {
        WriteSource* source = m_writeSources.at(j);
        int space = source->get_processable_buffer_space();
        QPair<int, WriteSource*> data(space, source);
        m_writersStatus.append(data);
    }

    for (int j=0; j<m_readSources.size(); ++j) {
        ReadSource* source = m_readSources.at(j);
        BufferStatus* status = source->get_buffer_status();

        if (status->fillStatus < 80 || status->out_of_sync()) {

            if ((status->fillStatus < m_readBufferFillStatus.load()) && !status->out_of_sync()) {
                m_readBufferFillStatus.store(status->fillStatus);
            }

            m_processableReadSources.append(source);

        }
    }



        for(int pair=0; pair<m_writersStatus.size(); ++pair) {
            WriteSource* source = m_writersStatus.at(pair).second;
            int space = m_writersStatus.at(pair).first;
            int prio = int(space  / source->get_chunck_size());

            // If the source stopped recording, it will write it's remaining samples in the next
            // process_buffers call, and unregister itself from this DiskIO instance!
            if ( space > 20 ||  ! source->is_recording() ) {

                if ((source->get_buffer_size() - space) < 8192) {
                    if (! m_hardDiskOverLoadCounter++) {
                        emit writeSourceBufferOverRun();
                    }
                }

                if (space > m_writeBufferFillStatus.load()) {
                    m_writeBufferFillStatus.store(space);
                }

                m_processableWriteSources.append(source);
            }
        }


        if (m_processableReadSources.size() > 0 || m_processableWriteSources.size() > 0 || m_processableSyncSources.size() > 0)  {
            return 1;
    }

    return 0;
}

/**
 *      Registers the ReadSource source. The source's RingBuffer will be initialized at this point.
 *
 *	Note: This function is thread save.
 * @param source The ReadSource to register
 */
void DiskIO::register_read_source (ReadSource* source )
{
    PENTER2;

    if (source->get_channel_count() == 0) {
        return;
    }

    source->set_diskio(this);
    source->prepare_rt_buffers(m_fileDecodeBuffer, m_sheet->get_transport_location());
        // QMetaObject::invokeMethod(this, "do_work()", Qt::QueuedConnection);

    QMutexLocker locker(&mutex);

    m_readSources.append(source);
}

/**
 *      Registers the WriteSource source. The source's RingBuffer will be initialized at this point.
 *
 *	Note: This function is thread save.
 * @param source The WriteSource to register
 */
void DiskIO::register_write_source( WriteSource * source )
{
    PENTER2;

    source->set_diskio(this);

    QMutexLocker locker(&mutex);

    m_writeSources.append(source);
}

/**
 * 	Unregisters the ReadSource from this DiskIO instance
 *
 *	Note: This function is Thread save.
 * @param source The ReadSource to be removed from the DiskIO instance.
 */
void DiskIO::unregister_read_source( ReadSource * source )
{
    QMutexLocker locker(&mutex);

    m_readSources.removeAll(source);
}


// internal function
void DiskIO::unregister_write_source( WriteSource * source )
{
    m_writeSources.removeAll(source);
}

/**
 *      Interupts any pending AudioSource's buffer processing, and returns from do_work().
 *	Use this before calling seek() to shorten the seek process.
 */
void DiskIO::prepare_for_seek( )
{
    PENTER;
    // Stop any processing in do_work()
    m_stopWork.store(true);
}

// Internal function
void DiskIO::update_time_usage(trav_time_t time)
{
    trav_time_t runcycleTime = time - m_doWorkStartTime;
    m_cpuTime->write(&runcycleTime, 1);
}

/**
 *
 * @return Returns the CPU time consumed by the DiskIO work thread
 */
float DiskIO::get_cpu_time( )
{
    trav_time_t currentTime = TTimeRef::get_nanoseconds_since_epoch();
    float totaltime = 0;
    trav_time_t value = 0;
    int read = m_cpuTime->read_space();

    while (read != 0) {
        read = m_cpuTime->read(&value, 1);
        totaltime += value;
    }

    audio_sample_t result = ( (totaltime  / (currentTime - m_lastdoWorkReadTime) ) * 100 );

    m_lastdoWorkReadTime = currentTime;

    return result;
}


/**
 * 	Get the status of the writebuffers.
 *
 * @return The status in procentual amount of the smallest remaining space in the writebuffers
 *		that could be used to write 'recording' data too.
 */
int DiskIO::get_write_buffers_fill_status( )
{
    int space = m_writeBufferFillStatus.load();
    m_writeBufferFillStatus.store(0);

    int size = int(audiodevice().get_sample_rate()) * writebuffertime;
    int status = int((float(size - space) / size) * 100);

    return status;
}

/**
 * 	Get the status of the readbuffers.
 *
 * @return The status is the procentual amount of the buffer which was most empty since the last call to this function
 */
int DiskIO::get_read_buffers_fill_status( )
{
    if (m_seeking.load()) {
        return 100;
    }

    int status = m_readBufferFillStatus.load();
    m_readBufferFillStatus.store(100);

    return status;
}

void DiskIO::stop_disk_thread( )
{
    PENTER;
    if (!m_diskThread.isRunning()) {
        return;
    }


    // Stop any processing in do_work()
    m_stopWork.store(true);

    // Exit the diskthreads event loop
    printf("DiskIO::stop_io: calling m_diskThread->exit(0)\n");
    m_diskThread.exit(0);

    // Wait for the Thread to return from it's event loop. 1000 ms should be (more then) enough,
    // if not, terminate this thread and print a warning!
    if ( ! m_diskThread.wait(2000) ) {
        qWarning("DiskIO :: Still running after 2 second wait, terminating!");
        m_diskThread.terminate();
    }
}

void DiskIO::set_resample_quality(int quality)
{
    m_resampleQuality = quality;
}

