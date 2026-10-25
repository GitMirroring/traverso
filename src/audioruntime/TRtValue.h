// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "TRtTransaction.h"

#include <functional>
#include <memory>

/**
 * @brief A value that the GUI thread and the audio thread both read, but only
 *        the GUI thread changes.
 *
 * The value is kept as snapshots: a published snapshot is never changed. A
 * change marks the value changed in a TRtTransaction; the GUI thread builds the
 * new snapshot once, when it needs it or when the transaction publishes it, and
 * the audio thread swaps it in at the start of a process cycle. Both threads may
 * read the same snapshot: the GUI side keeps its newest snapshot, the RT side
 * the one the audio thread uses.
 *
 * The audio thread never releases a snapshot: the one it swaps out is released
 * by the message queue worker thread, together with the step that carried the
 * new one.
 *
 * A pending step shares the RT side with this object, so this object (and its
 * owner) may be destroyed before the audio thread applied the step, e.g. a curve
 * of a clip that was only created for a moment. It must not be destroyed while
 * the audio thread still evaluates it.
 */
template <class T>
class TRtValue final : public TRtPublishable
{
public:
    using BuildSnapshot = std::function<std::shared_ptr<const T>()>;

    // buildSnapshot creates a snapshot from the owner's state, in the GUI
    // thread. There is no snapshot until the first change is published.
    explicit TRtValue(BuildSnapshot buildSnapshot)
        : m_buildSnapshot(std::move(buildSnapshot))
        , m_rtSide(std::make_shared<RtSide>())
    {
        Q_ASSERT(m_buildSnapshot);
    }

    // Starts with initialSnapshot on both sides, e.g. an empty list, so the
    // audio thread never gets nullptr
    TRtValue(BuildSnapshot buildSnapshot, std::shared_ptr<const T> initialSnapshot)
        : m_buildSnapshot(std::move(buildSnapshot))
        , m_guiSnapshot(initialSnapshot)
        , m_guiSnapshotOutdated(false)
        , m_rtSide(std::make_shared<RtSide>(RtSide{std::move(initialSnapshot)}))
    {
        Q_ASSERT(m_buildSnapshot);
    }

    ~TRtValue() override = default;

    TRtValue(const TRtValue&) = delete;
    TRtValue& operator=(const TRtValue&) = delete;

    // GUI thread

    // The owner's state changed, the new snapshot is built when it is needed
    void mark_changed(TRtTransaction& transaction)
    {
        Q_ASSERT_X(is_gui_thread(), "TRtValue::mark_changed", "GUI side used outside the GUI thread");
        m_guiSnapshotOutdated = true;
        publish_in(transaction);
    }

    // The newest snapshot, built if the owner changed since the last one
    const std::shared_ptr<const T>& gui_snapshot() const
    {
        Q_ASSERT_X(is_gui_thread(), "TRtValue::gui_snapshot", "GUI side used outside the GUI thread");
        if (m_guiSnapshotOutdated) {
            m_guiSnapshot = m_buildSnapshot();
            m_guiSnapshotOutdated = false;
        }
        return m_guiSnapshot;
    }

    // Audio thread

    // The snapshot the audio thread uses, nullptr until the first one is published
    const T* rt_snapshot() const T_REALTIME_SAFE
    {
        Q_ASSERT_X(!is_gui_thread(), "TRtValue::rt_snapshot", "RT side used in the GUI thread");
        return m_rtSide->snapshot.get();
    }

private:
    // The snapshot the audio thread uses
    struct RtSide
    {
        std::shared_ptr<const T> snapshot;
    };

    class SwapStep final : public TRtStep
    {
    public:
        SwapStep(std::shared_ptr<RtSide> rtSide, std::shared_ptr<const T> snapshot)
            : m_rtSide(std::move(rtSide)), m_snapshot(std::move(snapshot)) {}

        // Releases the snapshot the audio thread swapped out, in the worker
        // thread, and the RT side if its TRtValue is gone
        ~SwapStep() override = default;

        // Swapping two shared pointers only exchanges pointers, it does not
        // allocate, release or touch the reference counts
        void apply_in_audio_thread() T_REALTIME_SAFE override { m_rtSide->snapshot.swap(m_snapshot); }

    private:
        std::shared_ptr<RtSide>  m_rtSide;
        std::shared_ptr<const T> m_snapshot;
    };

    std::unique_ptr<TRtStep> create_publish_step() override
    {
        return std::make_unique<SwapStep>(m_rtSide, gui_snapshot());
    }

    BuildSnapshot                    m_buildSnapshot;
    mutable std::shared_ptr<const T> m_guiSnapshot;
    mutable bool                     m_guiSnapshotOutdated{true};
    std::shared_ptr<RtSide>          m_rtSide;
};
