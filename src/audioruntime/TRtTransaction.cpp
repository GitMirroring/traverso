/*
    Copyright (C) 2026 Remon Sijrier
    This file is part of Traverso
*/

#include "TRtTransaction.h"

#include "TAudioDevice.h"
#include "TAudioThreadMessageQueue.h"
#include "Debugger.h"

#include <QCoreApplication>
#include <QThread>
#include <chrono>
#include <vector>

static TRtTransaction* s_current = nullptr;

// All steps of one transaction. Applied in the audio thread, deleted in the
// message queue worker thread, which frees what the steps own.
struct TRtStepBatch
{
    std::vector<std::unique_ptr<TRtStep>> steps;
    std::chrono::nanoseconds              applyDuration{0};

    void apply_in_audio_thread()
    {
        // Reading the monotonic clock does not block
        const auto start = std::chrono::steady_clock::now();

        for (auto& step : steps) {
            step->apply_in_audio_thread();
        }

        applyDuration = std::chrono::steady_clock::now() - start;
    }

    // Message queue worker thread: how long the audio thread spent on the batch,
    // big batches (a sheet or project load) at debug level 1, all at level 2
    void report() const
    {
        const auto microseconds = double(applyDuration.count()) / 1000.0;
        if (steps.size() >= 10) {
            PMESG("TRtTransaction: applied %d steps in %.1f us", int(steps.size()), microseconds);
        } else {
            PMESG2("TRtTransaction: applied %d steps in %.1f us", int(steps.size()), microseconds);
        }
    }
};

bool TRtPublishable::is_gui_thread()
{
    return QThread::currentThread() == QCoreApplication::instance()->thread();
}

void TRtPublishable::publish_in(TRtTransaction& transaction)
{
    Q_ASSERT_X(is_gui_thread(), "TRtPublishable::publish_in", "Real-time state can only be changed in the GUI thread");
    transaction.publish_at_end(this);
}

TRtTransaction::TRtTransaction()
    : m_outer(s_current)
{
    Q_ASSERT_X(QThread::currentThread() == QCoreApplication::instance()->thread(),
               "TRtTransaction", "Real-time transactions can only be used in the GUI thread");

    s_current = this;
}

TRtTransaction::~TRtTransaction()
{
    s_current = m_outer;

    if (!m_outer) {
        commit();
    }
}

bool TRtTransaction::is_open()
{
    return s_current != nullptr;
}

void TRtTransaction::publish_at_end(TRtPublishable* publishable)
{
    // Only the outermost transaction publishes
    TRtTransaction* outermost = this;
    while (outermost->m_outer) {
        outermost = outermost->m_outer;
    }

    if (!outermost->m_toPublish.contains(publishable)) {
        outermost->m_toPublish.append(publishable);
    }
}

void TRtTransaction::commit()
{
    if (m_toPublish.isEmpty()) {
        return;
    }

    auto* batch = new TRtStepBatch;
    batch->steps.reserve(m_toPublish.size());

    for (TRtPublishable* publishable : std::as_const(m_toPublish)) {
        batch->steps.push_back(publishable->create_publish_step());
    }

    // Only possible while the audio device switches drivers, the next
    // driver's thread applies the changes then. Steps may outlive the lists
    // they publish (they share the RT side), so that is safe.
    if (!audiodevice().is_driver_loaded()) {
        PWARN("TRtTransaction: no audio driver loaded, the changes wait for the next one");
    }

    tsmp().post_rt_task(
        [batch]() { batch->apply_in_audio_thread(); },
        [batch]() { batch->report(); delete batch; });
}
