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

bool TRtListBase::is_gui_thread()
{
    return QThread::currentThread() == QCoreApplication::instance()->thread();
}

void TRtListBase::mark_dirty(TRtTransaction& transaction)
{
    Q_ASSERT_X(is_gui_thread(), "TRtListBase::mark_dirty", "TRtLists can only be changed in the GUI thread");
    transaction.mark_dirty(this);
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

void TRtTransaction::mark_dirty(TRtListBase* list)
{
    // Only the outermost transaction publishes
    TRtTransaction* outermost = this;
    while (outermost->m_outer) {
        outermost = outermost->m_outer;
    }

    if (!outermost->m_dirtyLists.contains(list)) {
        outermost->m_dirtyLists.append(list);
    }
}

void TRtTransaction::commit()
{
    if (m_dirtyLists.isEmpty()) {
        return;
    }

    auto* batch = new TRtStepBatch;
    batch->steps.reserve(m_dirtyLists.size());

    for (TRtListBase* list : std::as_const(m_dirtyLists)) {
        batch->steps.push_back(list->create_publish_step());
    }

    if (!audiodevice().is_driver_loaded()) {
        // Only possible while the audio device switches drivers or shuts down,
        // as it falls back to the dummy driver otherwise. Posting now could run
        // the steps after a restart, when the lists may be gone, so apply them
        // directly.
        PWARN("TRtTransaction: no audio driver loaded, applying changes directly");
        batch->apply_in_audio_thread();
        delete batch;
        return;
    }

    tsmp().post_rt_task(
        [batch]() { batch->apply_in_audio_thread(); },
        [batch]() { batch->report(); delete batch; });
}
