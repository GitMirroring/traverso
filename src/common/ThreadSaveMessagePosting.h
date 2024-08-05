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

$Id: TSMP.h,v 1.4 2008/02/11 10:11:52 r_sijrier Exp $
*/

#ifndef TSMP_H
#define TSMP_H

#include <QObject>

#include "cameron/readerwritercircularbuffer.h"

class ThreadSaveMessagePostingThread;

struct TSMPEvent {
    QObject* 	caller;
    void*		argument;
    int         slotindex;
    int         signalindex;
};


class ThreadSaveMessagePosting : public QObject
{
    Q_OBJECT

public:
    void prepare_event(TSMPEvent &event, QObject* caller, void* argument, const char* slotSignature, const char* signalSignature);

    void post_gui_event(const TSMPEvent &event);
    void post_rt_event(const TSMPEvent& event);

    void process_event(const TSMPEvent &event);
    void process_event_slot(const TSMPEvent& event);
    void process_event_signal(const TSMPEvent &event);

    void add_gui_event(QObject* caller, void* arg, const char* slotSignature, const char* signalSignature);

private:
    ThreadSaveMessagePosting();
    ~ThreadSaveMessagePosting();
    ThreadSaveMessagePosting(const ThreadSaveMessagePosting&);

    // allow this function to create one instance
    friend ThreadSaveMessagePosting& tsmp();

    // The AudioDevice instance is the _only_ one who
    // is allowed to call process_events_slot() !!
    friend class TAudioDevice;
    friend class ThreadSaveMessagePostingThread;

    moodycamel::BlockingReaderWriterCircularBuffer<TSMPEvent>*   m_postedFromGuiThreadQueue;
    moodycamel::BlockingReaderWriterCircularBuffer<TSMPEvent>*   m_postedFromRTThreadQueue;
    moodycamel::BlockingReaderWriterCircularBuffer<TSMPEvent>*   m_processedByRTThreadQueue;

    int             m_eventCounter;
    int             m_retryCount;
    ThreadSaveMessagePostingThread* m_threadSaveMessagePostingThread;

    void process_posted_gui_events();
    void process_processed_events_by_rt_thread_queue();

signals:
    void audioThreadEventBufferFull(QString);
};

// use this function to access the tsmp singleton pointer
ThreadSaveMessagePosting& tsmp();

#endif


//eof



