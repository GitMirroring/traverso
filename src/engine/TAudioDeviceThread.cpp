/*
Copyright (C) 2005-2007 Remon Sijrier

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

$Id: AudioDeviceThread.cpp,v 1.21 2007/10/20 17:38:19 r_sijrier Exp $
*/

#include "TAudioDeviceThread.h"

#include "TAudioDevice.h"

#if defined (Q_OS_UNIX)
#include <dlfcn.h>
#include <sys/resource.h>
#include <sched.h>
#endif

#include <sys/time.h>
#include <unistd.h>
#include <csignal>

#include "Debugger.h"

class WatchDogThread : public QThread
{
    TAudioDeviceThread* guardedThread;

public:
    WatchDogThread(TAudioDeviceThread* thread)
	{
		guardedThread = thread;
	}

protected:
	void run() override
	{
#if defined (Q_OS_UNIX) || defined (Q_OS_MAC)
		struct sched_param param;
		param.sched_priority = 90;
		if (pthread_setschedparam (pthread_self(), SCHED_FIFO, &param) != 0) {}
#endif


		while(true) {
			sleep(5);
//                         printf("Checking watchdogCheck...\n");

			if (guardedThread->watchdogCheck == 0) {
				qCritical("WatchDog timed out!");
//				guardedThread->terminate();
#if defined (Q_OS_UNIX) || defined (Q_OS_MAC)
				kill (-getpgrp(), SIGABRT);
#endif
			}

			guardedThread->watchdogCheck = 0;
		}
	}
};

TAudioDeviceThread::TAudioDeviceThread(TAudioDevice* device, bool realTime)
{
	m_device = device;
    m_realTime = realTime;
	setTerminationEnabled(true);

	watchdogCheck = 1;
}


void TAudioDeviceThread::run()
{
    run_on_cpu( 0 );

	
	WatchDogThread watchdog(this);
	watchdog.start();

    if (m_realTime) {
        become_realtime();
    }

    printf("TAudioDeviceThread: Starting Driver\n");
    if (m_device->start_driver() < 0) {
        printf("TAudioDeviceThread: Starting Driver Failed\n");
        watchdog.terminate();
		watchdog.wait();
		return;
	}

    printf("TAudioDeviceThread: Running\n");

	while (m_device->run_audio_thread()) {
        if (m_device->_run_cycle() < 0) {
            PERROR("Driver cycle error, exiting!");
            break;
        }
		watchdogCheck = 1;
	}
	
	watchdog.terminate();
	watchdog.wait();

    printf("TAudioDeviceThread: Bye.\n");
}

void TAudioDeviceThread::set_real_time(bool realTime)
{
    m_realTime = realTime;
}


int TAudioDeviceThread::become_realtime()
{
#if defined (Q_OS_UNIX) || defined (Q_OS_MAC)

	/* RTC stuff */
    struct sched_param param;
    param.sched_priority = 70;
    if (pthread_setschedparam (pthread_self(), SCHED_FIFO, &param) != 0) {
        m_device->driver_setup_message("AudioDeviceThread", tr("Unable to set Thread to realtime priority!!!"
            "This most likely results in unreliable playback/capture and "
            "lots of buffer underruns (== sound drops)."
            "In the worst case the program can even malfunction!"
            "Please make sure you run this program with realtime privileges!!!"), TAudioDevice::CRITICAL);
        return -1;
    } else {
        printf("AudioThread: Running with realtime priority\n");
        return 1;
    }

#endif

	return -1;
}


#if defined (Q_OS_UNIX)
typedef int* (*setaffinity_func_type)(pid_t,unsigned int,cpu_set_t *);
#endif

void TAudioDeviceThread::run_on_cpu( int cpu )
{
#if defined (Q_OS_UNIX)
    void *setaffinity_handle = dlopen(nullptr, RTLD_LAZY);
	
    setaffinity_func_type setaffinity_func = reinterpret_cast<setaffinity_func_type>(dlsym(setaffinity_handle, "sched_setaffinity"));
	
    if (setaffinity_func != nullptr) {
		cpu_set_t mask;
		CPU_ZERO(&mask);
		CPU_SET(cpu, &mask);
		if (setaffinity_func(0, sizeof(mask), &mask)) {
            printf("AudioThread: Unable to set CPU affinity\n");
		} else {
            printf("AudioThread: Running on CPU %d\n", cpu);
		}
	}
	else {
        printf("AudioDevice: Unable to set CPU affinity (glibc is too old)\n");
	}
#endif
}


