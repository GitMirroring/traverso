/*
Copyright (C) 2006-2026 Remon Sijrier

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

#pragma once

#include <QObject>
#include <QThread>
#include <memory>
#include <functional>
#include <type_traits>
#include "cameron/readerwritercircularbuffer.h"
#include "TRealTime.h"
#include "TRealTimeClosure.h"

/**
 * @brief An event passed between the GUI thread, the audio thread and the
 *        message queue worker thread (TAudioThreadMessageQueueThread).
 *
 * rtTask runs in the audio thread. guiCallback, despite its name, does NOT run
 * in the GUI thread: it runs in the message queue worker thread, after the
 * audio thread processed rtTask (or directly, for events posted by the audio
 * thread via post_to_gui_thread()).
 *
 * guiCallback may therefore only:
 * - free plain memory that nobody else references any more (this keeps freeing
 *   out of both the audio thread and the GUI thread)
 * - emit signals; Qt delivers them queued to receivers living in the GUI thread
 *
 * It must not call GUI or model functions directly, and must not delete
 * QObjects that live in the GUI thread. Such objects have to be deleted in the
 * GUI thread, e.g. by calling deleteLater() on them.
 */
struct TAudioThreadMessageQueueEvent {
    TRealTimeClosure                rtTask;
    std::move_only_function<void()> guiCallback{nullptr};
};

class TAudioThreadMessageQueueThread : public QThread
{
protected:
    void run() override;
};

class TAudioThreadMessageQueue : public QObject
{
    Q_OBJECT

public:
    /**
     * rtTask runs in the audio thread: a TRealTimeClosure, whose type checks
     * at compile time that it is real-time safe. guiSignal runs afterwards in
     * the message queue worker thread, not in the GUI thread. See
     * TAudioThreadMessageQueueEvent for what guiSignal may and may not do.
     */
    template<typename RTTask, typename GuiSignal = std::nullptr_t>
    void post_rt_task(RTTask&& rtTask, GuiSignal&& guiSignal = nullptr)
    {
        using DecayedGuiSignal = std::decay_t<GuiSignal>;
        using TargetType = std::move_only_function<void()>;

        // The GUI return signal
        if constexpr (!std::is_same_v<DecayedGuiSignal, TargetType> && !std::is_same_v<DecayedGuiSignal, std::nullptr_t>) {
            static_assert(sizeof(DecayedGuiSignal) <= 64,
                          "CRITICAL C++23 ERROR: GUI Signal lambda exceeds 64 bytes!");
            static_assert(std::is_trivially_copyable_v<DecayedGuiSignal>,
                          "CRITICAL C++23 ERROR: GUI Signal lambda contains non-trivial captures!");
        }

        TAudioThreadMessageQueueEvent event{};
        event.rtTask = TRealTimeClosure(std::forward<RTTask>(rtTask));
        event.guiCallback = std::forward<GuiSignal>(guiSignal);
        post_gui_event(std::move(event));
    }

    void post_gui_event(TAudioThreadMessageQueueEvent &&event);
    // Called from the audio thread. Despite the name, event.guiCallback runs in the
    // message queue worker thread, see TAudioThreadMessageQueueEvent.
    void post_to_gui_thread(TAudioThreadMessageQueueEvent &&event) T_REALTIME_SAFE;

private:
    TAudioThreadMessageQueue();
    ~TAudioThreadMessageQueue() override;
    TAudioThreadMessageQueue(const TAudioThreadMessageQueue&) = delete;

    friend TAudioThreadMessageQueue& tsmp() T_REALTIME_SAFE;
    // The one queue, made by the first call (tsmp())
    static TAudioThreadMessageQueue& make_instance();

    friend class TAudioDevice;
    friend class TAudioThreadMessageQueueThread;

    void process_posted_gui_events() T_REALTIME_SAFE;
    void process_processed_events_by_rt_thread_queue();

    std::unique_ptr<moodycamel::BlockingReaderWriterCircularBuffer<TAudioThreadMessageQueueEvent>> m_postedFromGuiThreadQueue;
    std::unique_ptr<moodycamel::BlockingReaderWriterCircularBuffer<TAudioThreadMessageQueueEvent>> m_postedFromRTThreadQueue;
    std::unique_ptr<moodycamel::BlockingReaderWriterCircularBuffer<TAudioThreadMessageQueueEvent>> m_processedByRTThreadQueue;

    TAudioThreadMessageQueueThread* m_TAudioThreadMessageQueueThread;
    std::atomic<int>  m_droppedRtEvents{0};
    std::atomic<bool> m_running{true};

public slots:
    void trigger_batch_start() { emit batchTransactionStarted(); }
    void trigger_batch_finish() { emit batchTransactionFinished(); }

signals:
    void batchTransactionStarted();
    void batchTransactionFinished();
    void queueOverflowDetected();
};

TAudioThreadMessageQueue& tsmp() T_REALTIME_SAFE;
