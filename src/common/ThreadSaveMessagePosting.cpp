/*
Copyright (C) 2006 - 2024 Remon Sijrier

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

$Id: TSMP.cpp,v 1.4 2008/02/11 10:11:52 r_sijrier Exp $
*/

#include "ThreadSaveMessagePosting.h"

#include "TAudioDevice.h"
#include "Debugger.h"
#include "TAudioDeviceSetup.h"

#include <QMetaMethod>
#include <QMessageBox>
#include <QCoreApplication>
#include <QThread>
#include <unistd.h>


/**
 * 	\class TSMP
 * 	\brief TSMP (Thread Save Message Postinge) is a singleton class to call
 *		functions (both signals and slots) in a thread save way without
 *		using any mutual exclusion primitives (mutex)
 *
 */

class ThreadSaveMessagePostingThread : public QThread
{
    void run() {
        while(true) {
            // printf("calling TSMP process_TSMP_signals\n");
            tsmp().process_processed_events_by_rt_thread_queue();
        }
    }
};


/**
 *
 * @return The ThreadSaveMessagePosting instance.
 */
ThreadSaveMessagePosting& tsmp()
{
    static ThreadSaveMessagePosting ThreadSaveAddRemove;
	return ThreadSaveAddRemove;
}

ThreadSaveMessagePosting::ThreadSaveMessagePosting()
{
    m_postedFromGuiThreadQueue = new moodycamel::BlockingReaderWriterCircularBuffer<TSMPEvent>(16384);
    m_postedFromRTThreadQueue = new moodycamel::BlockingReaderWriterCircularBuffer<TSMPEvent>(65536);
    m_processedByRTThreadQueue = new moodycamel::BlockingReaderWriterCircularBuffer<TSMPEvent>(65536 + 16384);

    m_eventCounter = 0;
    m_retryCount = 0;

    m_threadSaveMessagePostingThread = new ThreadSaveMessagePostingThread;
    m_threadSaveMessagePostingThread->start();
    m_threadSaveMessagePostingThread->moveToThread(m_threadSaveMessagePostingThread);
}

ThreadSaveMessagePosting::~ ThreadSaveMessagePosting( )
{
}


/**
 * 	Use this function to add events to the event queue when 
 * 	called from the GUI thread.
 *
 *  Blocks (buzy waits) if the event buffer is full
 *
 *	Note: This function should be called ONLY from the GUI thread! 
 * @param event  The event to add to the event queue
 */
void ThreadSaveMessagePosting::post_gui_event(const TSMPEvent &event )
{
    Q_ASSERT_X(this->thread() == QThread::currentThread(), "TSMP::add_event", "Adding event from other then GUI thread!!");

    if (!m_postedFromGuiThreadQueue->try_enqueue(event)) {
        // In Debug build do not accept overloads of the event queue, in non-debug mode this assert will do nothing
        // and the program will potentially stall the GUI thread for some time till the RT thread has processed pending events
        Q_ASSERT_X(true, "TSMP::post_gui_event", "Could not post gui event to posted from GUI thread queue, this is a problem that needs to be investigated by the developers");
        m_postedFromGuiThreadQueue->wait_enqueue(event);
    }

    m_eventCounter++;
}

/**
 * 	Use this function to add events to the event queue when
 * 	called from the audio processing (real time) thread
 *
 *	Note: This function should be called ONLY from the realtime audio thread
 *
 * @param event The event to add to the event queue
 */
void ThreadSaveMessagePosting::post_rt_event(const TSMPEvent &event )
{
    Q_ASSERT_X(this->thread() != QThread::currentThread(), "TSMP::post_rt_event", "Adding event from NON-RT Thread!!");

    if (!m_postedFromRTThreadQueue->try_enqueue(event)) {
        // In Debug build do not accept overloads of the event queue, in non-debug mode this assert will do nothing
        // and the program will potentially stall the RT thread for some time till the system has processed pending events
        // this could occur in rare cases when in freewheeling mode and nothing to process in RT Thread
        Q_ASSERT_X(true, "TSMP::post_rt_event", "Could not post rt event to posted from RT thread queue, this is a problem that needs to be investigated by the developers");
        m_postedFromRTThreadQueue->wait_enqueue(event);
    }
}


//
//  Function called in RealTime AudioThread processing path
//
void ThreadSaveMessagePosting::process_posted_gui_events( )
{
   TSMPEvent event;

    while (m_postedFromGuiThreadQueue->try_dequeue(event)) {
        process_event_slot(event);
        // printf("Processed %s slot: %s, signal: %s\n", event.caller->metaObject()->className(),
        //        (event.slotindex >= 0) ? event.caller->metaObject()->method(event.slotindex).methodSignature().data() : "no_slot_supplied",
        //        (event.signalindex >= 0) ? event.caller->metaObject()->method(event.signalindex).methodSignature().data() : "so_signal_supplied");

        // The gui event wants to emit a signal so we move the event back
        // into the GUI event loop for the signal to be emitted
        if (event.signalindex >= 0) {
            if (!m_processedByRTThreadQueue->try_enqueue(event)) {
                // In Debug build do not accept overloads of the event queue, in non-debug mode this assert will do nothing
                // and the program will potentially stall the RT thread for some time till the system has processed pending events
                // this could occur in rare cases when in freewheeling mode and nothing to process in RT Thread
                Q_ASSERT_X(true, "TSMP::process_posted_gui_events", "Could not post RT event to processed by RT thread queue, this is a problem that needs to be investigated by the developers");
                m_processedByRTThreadQueue->wait_enqueue(event);
            }
        } else {
            --m_eventCounter;
        }
    }
}

// Called by TSMPThread which is allowed to block on the wait_dequeue()
void ThreadSaveMessagePosting::process_processed_events_by_rt_thread_queue( )
{
    Q_ASSERT_X(m_threadSaveMessagePostingThread->thread() == QThread::currentThread(), "TSMP::process_processed_events_by_rt_thread_queue", "Runs in wrong trhead");

    TSMPEvent event;

    while(m_processedByRTThreadQueue->try_dequeue(event)) {
        process_event_signal(event);
    }

    while(m_postedFromRTThreadQueue->try_dequeue(event)) {
        process_event_signal(event);
    }

    // Block the TSMPThread until new events are posted to the m_postedFromRTThreadQueue
    // This will happen every run_cycle from AudioDevice
    m_postedFromRTThreadQueue->wait_dequeue(event);
    process_event_signal(event);


    --m_eventCounter;
    m_retryCount++;
	
    if (m_retryCount > 200)
	{
		if (audiodevice().get_driver_type() != "Dummy") {
            QMessageBox::critical( nullptr,
				tr("Traverso - Malfunction!"), 
				tr("The Audiodriver Thread seems to be stalled/stopped, but Traverso didn't ask for it!\n"
				"This effectively makes Traverso unusable, since it relies heavily on the AudioDriver Thread\n"
				"To ensure proper operation, Traverso will fallback to the 'Dummy'.\n"
                "Potential issues why this can show up are: \n\n"
				"* You're not running with real time privileges! Please make sure this is setup properly.\n\n"
				"* The audio chipset isn't supported (completely), you probably have to turn off some of it's features.\n"
				"\nFor more information, see the Help file, section: \n\n AudioDriver: 'Thread stalled error'\n\n"),
                QMessageBox::Ok);
            TAudioDeviceSetup audioDeviceSetup;
            audioDeviceSetup.set_driver_type("Dummy");
            audiodevice().set_parameters(audioDeviceSetup);
			m_retryCount = 0;
		} else {
            QMessageBox::critical( nullptr,
				tr("Traverso - Fatal!"), 
				tr("The Null AudioDriver stalled too, exiting application!"),
                QMessageBox::Ok);
			QCoreApplication::exit(-1);
		}
	}
	
	if (m_eventCounter <= 0) {
		m_retryCount = 0;
	}
}


/**
*	This function can be used to process the events 'slot' part.
*	Usefull when you have a ThreadSaveMessagePosting event, but don't want/need to use TSMP
*	to call the events slot in a thread save way
*
* @param event The TSMPEvent to be processed
*/
void ThreadSaveMessagePosting::process_event_slot(const TSMPEvent& event )
{
    Q_ASSERT(event.slotindex >= 0);

    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&event.argument)) };

    if ( ! (event.caller->qt_metacall(QMetaObject::InvokeMetaMethod, event.slotindex, _a) < 0) ) {
        qDebug("TSMP::process_event_slot failed (%s::%s)", event.caller->metaObject()->className(), event.caller->metaObject()->method(event.slotindex).methodSignature().data());
    }
}

/**
*	This function can be used to process the events 'signal' part.
*	Usefull when you have a ThreadSaveMessagePosting event, but don't want/need to use TSMP
*	to call the events signal in a thread save way
*
* @param event The TSMPEvent to be processed
*/
void ThreadSaveMessagePosting::process_event_signal(const TSMPEvent & event )
{
    Q_ASSERT(event.signalindex >= 0);

    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&event.argument))};

    if ( ! (event.caller->qt_metacall(QMetaObject::InvokeMetaMethod, event.signalindex, _a) < 0) ) {
            qDebug("TSMP::process_event_signal failed (%s::%s)", event.caller->metaObject()->className(), event.caller->metaObject()->method(event.signalindex).methodSignature().data());
    }
}

/**
*	Convenience function. Calls both process_event_slot() and process_event_signal()
*
*	\sa process_event_slot() \sa process_event_signal()
*
*	Note: This function doesn't provide the thread safetyness you get with
*		the add_event() function!
*
* @param event The TSMPEvent to be processed
*/
void ThreadSaveMessagePosting::process_event(const TSMPEvent & event )
{
	process_event_slot(event);
	process_event_signal(event);
}

void ThreadSaveMessagePosting::add_gui_event(QObject *caller, void *arg, const char *slotSignature, const char *signalSignature)
{
    PENTER;
    TSMPEvent event;
    prepare_event(event, caller, arg, slotSignature, signalSignature);
    post_gui_event(event);
}

/**
 */
void ThreadSaveMessagePosting::prepare_event(TSMPEvent &event, QObject* caller, void* argument, const char* slotSignature, const char* signalSignature )
{
    PENTER3;
    event.caller = caller;
    event.argument = argument;

    event.slotindex = caller->metaObject()->indexOfMethod(slotSignature);
    event.signalindex = caller->metaObject()->indexOfMethod(signalSignature);
}

//eof

