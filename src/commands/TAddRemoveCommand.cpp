/*
    Copyright (C) 2005-2026 Remon Sijrier
    This file is part of Traverso
    Fully modernized to pure C++23 using persistent factory closures.
*/

#include "TAddRemoveCommand.h"
#include "TAudioThreadMessageQueue.h"
#include "TContextItem.h"
#include "TContextPointer.h"
#include "TSession.h"

TAddRemoveCommand::TAddRemoveCommand(
    TContextItem* parent,
    void* arg,
    bool historable,
    TSession* sheet,
    std::function<void()> doMethod,   std::function<void()> doSignal,
    std::function<void()> undoMethod, std::function<void()> undoSignal,
    const QString& des)
    : TCommand(parent, des)
    , m_parentItem(parent)
    , m_arg(arg)
    , m_sheet(sheet)
    , m_doMethod(std::move(doMethod))
    , m_undoMethod(std::move(undoMethod))
    , m_doSignal(std::move(doSignal))
    , m_undoSignal(std::move(undoSignal))
    , m_instantanious(false)
{
    if (!historable) {
        set_do_not_push_to_historystack();
    }
}

TAddRemoveCommand::TAddRemoveCommand(
    TContextItem* parent,
    TContextItem* item,
    bool historable,
    TSession* sheet,
    std::function<void()> doMethod,   std::function<void()> doSignal,
    std::function<void()> undoMethod, std::function<void()> undoSignal,
    const QString& des)
    : TCommand(parent, des)
    , m_parentItem(parent)
    , m_arg(item)
    , m_sheet(sheet)
    , m_doMethod(std::move(doMethod))
    , m_undoMethod(std::move(undoMethod))
    , m_doSignal(std::move(doSignal))
    , m_undoSignal(std::move(undoSignal))
    , m_instantanious(false)
{
    if (!historable) {
        set_do_not_push_to_historystack();
    }

    if (item && item->has_active_context()) {
        cpointer().remove_from_active_context_list(item);
    }
}

int TAddRemoveCommand::do_action()
{
    return un_redo_action(m_doMethod, m_doSignal);
}

int TAddRemoveCommand::undo_action()
{
    return un_redo_action(m_undoMethod, m_undoSignal);
}

void TAddRemoveCommand::set_instantanious(bool instant)
{
    m_instantanious = instant;
}

int TAddRemoveCommand::un_redo_action(const std::function<void()>& method, const std::function<void()>& signal)
{
    bool executeSynchrone = m_instantanious;

    if (m_sheet && !m_sheet->is_transport_rolling()) {
        executeSynchrone = true;
    }

    if (executeSynchrone) {
        if (method) method();
        if (signal) signal();
        return 1;
    }

    if (&method == &m_doMethod) {
        tsmp().post_rt_task(
            [this]() { if (m_doMethod) m_doMethod(); },
            [this]() { if (m_doSignal) m_doSignal(); }
            );
    } else {
        tsmp().post_rt_task(
            [this]() { if (m_undoMethod) m_undoMethod(); },
            [this]() { if (m_undoSignal) m_undoSignal(); }
            );
    }


    return 1;
}
