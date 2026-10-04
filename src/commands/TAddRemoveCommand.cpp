/*
    Copyright (C) 2005-2026 Remon Sijrier
    This file is part of Traverso
*/

#include "TAddRemoveCommand.h"
#include "TAudioThreadMessageQueue.h"
#include "TContextItem.h"
#include "TContextPointer.h"
#include "TSession.h"

// --- IMPLEMENTATION CONSTRUCTOR 1 (GENERIC PAYLOAD) ---
TAddRemoveCommand::TAddRemoveCommand(
    TContextItem* parent,
    void* arg,
    bool historable,
    TSession* sheet,
    std::move_only_function<void()>&& doMethod,   std::move_only_function<void()>&& doSignal,
    std::move_only_function<void()>&& undoMethod, std::move_only_function<void()>&& undoSignal,
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

// --- IMPLEMENTATION CONSTRUCTOR 2 (CONTEXT VISUAL ITEM) ---
TAddRemoveCommand::TAddRemoveCommand(
    TContextItem* parent,
    TContextItem* item,
    bool historable,
    TSession* sheet,
    std::move_only_function<void()>&& doMethod,   std::move_only_function<void()>&& doSignal,
    std::move_only_function<void()>&& undoMethod, std::move_only_function<void()>&& undoSignal,
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

    // Context cleanup:
    // Safely removes the disappearing item from active cursor/mouse context mapping grids.
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

/**
 * @brief MASTER ROUTER: Evaluates the session transport state to dictate execution paths.
 *        Guarantees low-overhead synchronous execution when the audio engine transport is stopped.
 */
int TAddRemoveCommand::un_redo_action(std::move_only_function<void()>& method, std::move_only_function<void()>& signal)
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

    tsmp().post_rt_task(std::move(method), std::move(signal));

    return 1;
}

