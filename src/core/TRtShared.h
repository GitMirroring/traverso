// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "TRtTransaction.h"

#include <functional>
#include <memory>

/**
 * @brief An immutable value that the GUI thread and the audio thread share.
 *
 * A published version is never changed. A change marks the value changed in a
 * TRtTransaction; the GUI thread builds the new version once, when it needs it
 * or when the transaction publishes, and the audio thread swaps it in at the
 * start of a process cycle. Both threads may read the same version: the GUI
 * thread keeps its current version, the audio thread the one it uses.
 *
 * The audio thread never releases a version: the version it swaps out is
 * released by the message queue worker thread, together with the step that
 * carried the new one.
 *
 * A pending step shares the version the audio thread uses with this object, so
 * this object (and its owner) may be destroyed before the audio thread applied
 * the step, e.g. a curve of a clip that was only created for a moment. It must
 * not be destroyed while the audio thread still evaluates it.
 */
template <class T>
class TRtShared final : public TRtListBase
{
public:
    using Builder = std::function<std::shared_ptr<const T>()>;

    // builder creates the current version from the owner's state, in the GUI
    // thread. There is no version until the first change is published.
    explicit TRtShared(Builder builder)
        : m_builder(std::move(builder))
        , m_rtSlot(std::make_shared<RtSlot>())
    {
        Q_ASSERT(m_builder);
    }

    ~TRtShared() override = default;

    TRtShared(const TRtShared&) = delete;
    TRtShared& operator=(const TRtShared&) = delete;

    // GUI thread

    // The owner's state changed, the new version is built when it is needed
    void mark_changed(TRtTransaction& transaction)
    {
        Q_ASSERT_X(is_gui_thread(), "TRtShared::mark_changed", "GUI side used outside the GUI thread");
        m_changed = true;
        mark_dirty(transaction);
    }

    // The current version, built if the owner changed since the last one
    const std::shared_ptr<const T>& current() const
    {
        Q_ASSERT_X(is_gui_thread(), "TRtShared::current", "GUI side used outside the GUI thread");
        if (m_changed) {
            m_current = m_builder();
            m_changed = false;
        }
        return m_current;
    }

    // Audio thread

    // The version the audio thread uses, nullptr until the first one is published
    const T* rt_current() const
    {
        Q_ASSERT_X(!is_gui_thread(), "TRtShared::rt_current", "audio thread side used in the GUI thread");
        return m_rtSlot->version.get();
    }

private:
    // The version the audio thread uses
    struct RtSlot
    {
        std::shared_ptr<const T> version;
    };

    class SwapStep final : public TRtStep
    {
    public:
        SwapStep(std::shared_ptr<RtSlot> slot, std::shared_ptr<const T> version)
            : m_slot(std::move(slot)), m_version(std::move(version)) {}

        // Releases the version the audio thread swapped out, in the worker
        // thread, and the slot if its TRtShared is gone
        ~SwapStep() override = default;

        // Swapping two shared pointers only exchanges pointers, it does not
        // allocate, release or touch the reference counts
        void apply_in_audio_thread() override { m_slot->version.swap(m_version); }

    private:
        std::shared_ptr<RtSlot>  m_slot;
        std::shared_ptr<const T> m_version;
    };

    std::unique_ptr<TRtStep> create_publish_step() override
    {
        return std::make_unique<SwapStep>(m_rtSlot, current());
    }

    Builder                          m_builder;
    mutable std::shared_ptr<const T> m_current;
    mutable bool                     m_changed{true};
    std::shared_ptr<RtSlot>          m_rtSlot;
};
