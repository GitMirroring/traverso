/*
Copyright (C) 2006 Remon Sijrier 

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

$Id: Tsar.cpp,v 1.4 2008/02/11 10:11:52 r_sijrier Exp $
*/

#include "Tsar.h"

#include "AudioDevice.h"
#include <QMetaMethod>
#include <QMessageBox>
#include <QCoreApplication>
#include <QThread>
#include <QTimerEvent>
#include <unistd.h>

// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"

/**
 * 	\class Tsar
 * 	\brief Tsar (Thread Save Add and Remove) is a singleton class to call  
 *		functions (both signals and slots) in a thread save way without
 *		using any mutual exclusion primitives (mutex)
 *
 */


/**
 * 
 * @return The Tsar instance. 
 */
Tsar& tsar()
{
	static Tsar ThreadSaveAddRemove;
	return ThreadSaveAddRemove;
}

Tsar::Tsar()
{
	m_eventCounter = 0;

    size_t guiThreadEventsBufferSize = 10000;
    size_t audioThreadEventsBufferSize = 1000;

    m_guiThreadEventBuffer = new RingBufferNPT<TsarEvent>(guiThreadEventsBufferSize);
    m_audioThreadEventBuffer = new RingBufferNPT<TsarEvent>(audioThreadEventsBufferSize);
    m_processedEventsSlot = new RingBufferNPT<TsarEvent>(guiThreadEventsBufferSize);

    m_eventBuffers.append(m_guiThreadEventBuffer);
    m_eventBuffers.append(m_audioThreadEventBuffer);

	m_retryCount = 0;
	
#if defined (THREAD_CHECK)
    m_threadPointer = QThread::currentThread();
#endif

    m_timer.start(20, this);
}

Tsar::~ Tsar( )
{
    foreach(RingBufferNPT<TsarEvent>* eventBuffer, m_eventBuffers) {
		delete eventBuffer;
	}
    delete m_processedEventsSlot;
}

void Tsar::timerEvent(QTimerEvent *event)
{
        if (event->timerId() == m_timer.timerId()) {
                process_events_signal();
        }
}

/**
 * 	Use this function to add events to the event queue when 
 * 	called from the GUI thread.
 *
 *	Note: This function should be called ONLY from the GUI thread! 
 * @param event  The event to add to the event queue
 */
bool Tsar::add_event(TsarEvent& event )
{
#if defined (THREAD_CHECK)
    Q_ASSERT_X(m_threadPointer == QThread::currentThread(), "Tsar::add_event", "Adding event from other then GUI thread!!");
#endif
    if (m_guiThreadEventBuffer->write(&event, 1) == 1) {
		m_eventCounter++;
		return true;
	}
	m_retryCount = 0;
	return false;
}

/**
 * 	Use this function to add events to the event queue when  
 * 	called from the audio processing (real time) thread
 *
 *	Note: This function should be called ONLY from the realtime audio thread and has a 
 *	non blocking behaviour! (That is, it's a real time save function)
 *
 * @param event The event to add to the event queue
 */
void Tsar::add_rt_event( TsarEvent& event )
{
#if defined (THREAD_CHECK)
    Q_ASSERT_X(m_threadPointer != QThread::currentThread(), "Tsar::add_rt_event", "Adding event from NON-RT Thread!!");
#endif

    if (m_audioThreadEventBuffer->write(&event, 1) != 1) {
        emit audioThreadEventBufferFull(QString("Tsar::add_rt_event: Event lost due no write space in event buffer: %1::%2, (signal: %3)").arg(
            event.caller->metaObject()->className(),
            (event.slotindex >= 0) ? event.caller->metaObject()->method(event.slotindex).methodSignature().data() : "",
            (event.signalindex >= 0) ? event.caller->metaObject()->method(event.signalindex).methodSignature().data() : ""));
    }
}

//
//  Function called in RealTime AudioThread processing path
//
void Tsar::process_events_slot( )
{
#define profile

#if defined (profile)
    trav_time_t starttime = get_microseconds();
#endif

    for (int i=0; i<m_eventBuffers.size(); ++i) {
        RingBufferNPT<TsarEvent>* eventBuffer = m_eventBuffers.at(i);
		
        int processedEvents = 0;
        size_t availableEvents = eventBuffer->read_space();

        while((availableEvents > 0) && (processedEvents < 200)) {
            TsarEvent event;

            if (eventBuffer->read(&event, 1) == 1) {
                process_event_slot(event);
                // printf("Processed %s slot: %s, signal: %s\n", event.caller->metaObject()->className(),
                //        (event.slotindex >= 0) ? event.caller->metaObject()->method(event.slotindex).methodSignature().data() : "no_slot_supplied",
                //        (event.signalindex >= 0) ? event.caller->metaObject()->method(event.signalindex).methodSignature().data() : "so_signal_supplied");

                m_processedEventsSlot->write(&event, 1);
                --availableEvents;
            }

            ++processedEvents;
        }
    }
#if defined (profile)
    int processtime = int(get_microseconds() - starttime);
    if (processtime > 10)
        printf("Process time: %d useconds\n\n", processtime);
#endif
}

void Tsar::process_events_signal( )
{
	
    while(m_processedEventsSlot->read_space() >= 1 ) {
		TsarEvent event;
		// Read one TsarEvent from the processed events ringbuffer 'queue'
        m_processedEventsSlot->read(&event, 1);
		
		process_event_signal(event);
		
		--m_eventCounter;
// 		printf("finish_processed_objects:: Count is %d\n", m_eventCounter);
	}
	
	m_retryCount++;
	
    if (m_retryCount > 200)
	{
		if (audiodevice().get_driver_type() != "Null Driver") {
            QMessageBox::critical( nullptr,
				tr("Traverso - Malfunction!"), 
				tr("The Audiodriver Thread seems to be stalled/stopped, but Traverso didn't ask for it!\n"
				"This effectively makes Traverso unusable, since it relies heavily on the AudioDriver Thread\n"
				"To ensure proper operation, Traverso will fallback to the 'Null Driver'.\n"
				"Potential issues why this can show up are: \n\n"
				"* You're not running with real time privileges! Please make sure this is setup properly.\n\n"
				"* The audio chipset isn't supported (completely), you probably have to turn off some of it's features.\n"
				"\nFor more information, see the Help file, section: \n\n AudioDriver: 'Thread stalled error'\n\n"),
                QMessageBox::Ok);
            TAudioDeviceSetup ads;
            ads.driverType = "Null Driver";
            audiodevice().set_parameters(ads);
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
 * 	Creates a Tsar event. Add the tsar event to the event queue by calling add_event()
 * 	If you need to add an event from the real time audio processing thread, use
 * 	add_rt_event() instead!
 *
 *	Note: This function can be called both from the GUI and realtime audio thread and has a 
 *	non blocking behaviour! (That is, it's a real time save function)
 *
 * @param caller	The calling object, needs to be derived from a QObject
 * @param argument 	The slot and/or signal argument which can be of any type.
 * @param slotSignature The 'signature' of the calling objects slot (equals the name of the slot function)
 * @param signalSignature The 'signature' of the calling objects signal (equals the name of the signal function) 
 * @return The newly created event.
 */
TsarEvent Tsar::create_event( QObject* caller, void* argument, const char* slotSignature, const char* signalSignature )
{
	PENTER3;
	TsarEvent event;
	event.caller = caller;
	event.argument = argument;
    int index;
	
	if (qstrlen(slotSignature) > 0) {
		index = caller->metaObject()->indexOfMethod(slotSignature);
		if (index < 0) {
            PWARN(QString("Slot signature contains whitespaces, please remove to avoid unneeded processing (%1::%2)").arg(caller->metaObject()->className(), slotSignature).toLatin1().data());
			QByteArray norm = QMetaObject::normalizedSignature(slotSignature);
			index = caller->metaObject()->indexOfMethod(norm.constData());
			if (index < 0) {
//				PERROR("Couldn't find a valid index for %s", slotSignature);
			}
		}
		event.slotindex = index;
	} else {
		event.slotindex = -1;
	}
	
	if (qstrlen(signalSignature) > 0) {
        index = caller->metaObject()->indexOfMethod(signalSignature);
        if (index < 0) {
            PWARN(QString("Signal signature contains whitespaces, please remove to avoid unneeded processing (%1::%2)").arg(caller->metaObject()->className(), signalSignature).toLatin1().data());
			QByteArray norm = QMetaObject::normalizedSignature(signalSignature);
            index = caller->metaObject()->indexOfMethod(norm.constData());
        }
		event.signalindex = index; 
	} else {
		event.signalindex = -1; 
	}
	
	event.valid = true;
	
	return event;
}

/**
*	This function can be used to process the events 'slot' part.
*	Usefull when you have a Tsar event, but don't want/need to use tsar
*	to call the events slot in a thread save way
*
*	Note: This function doesn't provide the thread safetyness you get with
*		the add_event() function!
*
* @param event The TsarEvent to be processed 
*/
void Tsar::process_event_slot(const TsarEvent& event )
{
	// If there is an object to be added, do the magic to call the slot :-)
	if (event.slotindex > -1) {

        void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&event.argument)) };
		
		// This equals QMetaObject::invokeMethod(), without type checking. But we know that the types
		// are the correct ones, and will be casted just fine!
		if ( ! (event.caller->qt_metacall(QMetaObject::InvokeMetaMethod, event.slotindex, _a) < 0) ) {
            qDebug("Tsar::process_event_slot failed (%s::%s)", event.caller->metaObject()->className(), event.caller->metaObject()->method(event.slotindex).methodSignature().data());
		}
	}
}

/**
*	This function can be used to process the events 'signal' part.
*	Usefull when you have a Tsar event, but don't want/need to use tsar
*	to call the events signal in a thread save way
*
*	Note: This function doesn't provide the thread safetyness you get with
*		the add_event() function!
*
* @param event The TsarEvent to be processed 
*/
void Tsar::process_event_signal(const TsarEvent & event )
{
	// In case the signalindex > -1, emit the signal!
	if (event.signalindex > -1) {

                void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&event.argument)) };

                // This equals QMetaObject::invokeMethod(), without type checking. But we know that the types
                // are the correct ones, and will be casted just fine!
                if ( ! (event.caller->qt_metacall(QMetaObject::InvokeMetaMethod, event.signalindex, _a) < 0) ) {
                        qDebug("Tsar::process_event_signal failed (%s::%s)", event.caller->metaObject()->className(), event.caller->metaObject()->method(event.signalindex).methodSignature().data());
                }
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
* @param event The TsarEvent to be processed 
*/
void Tsar::process_event(const TsarEvent & event )
{
	process_event_slot(event);
	process_event_signal(event);
}

void Tsar::rt_thread_emit(QObject *cal, void* arg, const char* signalSignature)
{
    TsarEvent event;
    event.caller = cal;
    event.argument = arg;
    event.slotindex = -1;
    int retrievedsignalindex = cal->metaObject()->indexOfSignal(signalSignature);
    Q_ASSERT(retrievedsignalindex >= 0);
    event.signalindex = retrievedsignalindex;
    event.valid = true;
    add_rt_event(event);
}

void Tsar::thread_save_invoke_and_emit_signal(QObject *caller, void *arg, const char *slotSignature, const char *signalSignature)
{
    TsarEvent event = tsar().create_event(caller, arg, slotSignature, signalSignature);
    while (!add_event(event)) {
        std::cout << "THREAD_SAVE_INVOKE: failed to add event, trying again\n";
#if defined (Q_OS_WIN)
        Sleep(2);
#else
        usleep(2 * 1000);
#endif
    }
}

//eof

