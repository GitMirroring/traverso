/*
    Copyright (C) 2026 Remon Sijrier
    This file is part of Traverso
*/

#pragma once

#include "TRtValue.h"

#include <QList>
#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

/**
 * @brief A list of items that is read by the audio thread.
 *
 * The GUI thread owns the list: it is the single source of truth and only the
 * GUI thread changes it. The audio thread reads an immutable snapshot of it, a
 * TRtValue holding a vector of the item pointers. Every change publishes the
 * list in the TRtTransaction it is given, which builds a new snapshot in the
 * GUI thread and lets the audio thread swap it in.
 *
 * Replaces TRealTimeLinkedList: the audio thread only swaps a pointer, so the
 * cost in the audio thread does not depend on the size of the list.
 *
 * Like TRtValue, the list may be destroyed while a step is pending, but not
 * while the audio thread still iterates it.
 */
template <class T>
class TRtList final
{
public:
    using LessThan = std::function<bool(const T*, const T*)>;

    TRtList() : TRtList(LessThan()) {}

    // Keeps the list sorted by lessThan, e.g. clips by start location. The
    // order is fixed for the list's lifetime, so it never changes unpublished.
    explicit TRtList(LessThan lessThan)
        : m_lessThan(std::move(lessThan))
        , m_snapshot([this]() { return build_snapshot(); }, std::make_shared<std::vector<T*>>())
    {}

    TRtList(const TRtList&) = delete;
    TRtList& operator=(const TRtList&) = delete;

    // GUI thread

    const QList<T*>& gui_items() const
    {
        Q_ASSERT_X(TRtPublishable::is_gui_thread(), "TRtList::gui_items", "GUI side used outside the GUI thread");
        return m_guiItems;
    }

    void add(T* item, TRtTransaction& transaction)
    {
        m_guiItems.append(item);
        sort();
        m_snapshot.mark_changed(transaction);
    }

    void remove(T* item, TRtTransaction& transaction)
    {
        m_guiItems.removeAll(item);
        m_snapshot.mark_changed(transaction);
    }

    // Call after the sort order of items changed, e.g. a clip moved
    void resort(TRtTransaction& transaction)
    {
        sort();
        m_snapshot.mark_changed(transaction);
    }

    // Audio thread

    const std::vector<T*>& rt_items() const
    {
        return *m_snapshot.rt_snapshot();
    }

private:
    std::shared_ptr<const std::vector<T*>> build_snapshot() const
    {
        return std::make_shared<std::vector<T*>>(m_guiItems.begin(), m_guiItems.end());
    }

    void sort()
    {
        if (m_lessThan) {
            std::stable_sort(m_guiItems.begin(), m_guiItems.end(), m_lessThan);
        }
    }

    QList<T*>                         m_guiItems;
    LessThan                          m_lessThan;
    TRtValue<std::vector<T*>>         m_snapshot;
};
