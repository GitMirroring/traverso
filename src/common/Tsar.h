/*
Copyright (C) 2006-2019 Remon Sijrier

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

$Id: Tsar.h,v 1.4 2008/02/11 10:11:52 r_sijrier Exp $
*/

#ifndef TSAR_H
#define TSAR_H

#include <QObject>
#include <QBasicTimer>
#include <QByteArray>
#include "RingBufferNPT.h"
#include "qthread.h"


struct TsarEvent {
    // used for slot invokation stuff
    QObject* 	caller;
    void*		argument;
    int		slotindex;

    // Used for the signal emiting stuff
    int signalindex;

    bool valid;
};

class Tsar : public QObject
{
    Q_OBJECT

public:
    TsarEvent create_event(QObject* caller, void* argument, const char* slotSignature, const char* signalSignature);

    bool add_event(TsarEvent& event);
    void add_rt_event(TsarEvent& event);
    void process_event_slot(const TsarEvent& event);
    void process_event_signal(const TsarEvent& event);
    void process_event(const TsarEvent& event);

    void rt_thread_emit(QObject *cal, void* arg, const char* signalSignature);
    // Pass empty string to signalSignature if no signal has to be emitted
    void thread_save_invoke_and_emit_signal(QObject* caller, void* arg, const char* slotSignature, const char* signalSignature);

protected:
    void timerEvent(QTimerEvent *event);

private:
    Tsar();
    ~Tsar();
    Tsar(const Tsar&);

    // allow this function to create one instance
    friend Tsar& tsar();
    // The AudioDevice instance is the _only_ one who
    // is allowed to call process_events() !!
    friend class AudioDevice;

    QList<RingBufferNPT<TsarEvent>*>	m_eventBuffers;
    RingBufferNPT<TsarEvent>*           m_guiThreadEventBuffer;
    RingBufferNPT<TsarEvent>*           m_audioThreadEventBuffer;
    RingBufferNPT<TsarEvent>*           m_processedEventsSlot;
    QBasicTimer                         m_timer;
    int                                 m_eventCounter;
    int                                 m_retryCount;

#if defined (THREAD_CHECK)
    QThread*	m_threadPointer;
#endif

    void process_events_slot();
    void process_events_signal();

signals:
    void audioThreadEventBufferFull(QString);
};

// use this function to access the tsar singleton pointer
Tsar& tsar();

#endif


//eof



