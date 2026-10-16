/*
    Copyright (C) 2005-2026 Remon Sijrier
    This file is part of Traverso
*/

#include "TAddRemoveCommand.h"
#include "TRtTransaction.h"

TAddRemoveCommand::TAddRemoveCommand(
    TContextItem* parent,
    bool historable,
    std::function<void()> doMethod,
    std::function<void()> undoMethod,
    const QString& des)
    : TCommand(parent, des)
    , m_doMethod(std::move(doMethod))
    , m_undoMethod(std::move(undoMethod))
{
    if (!historable) {
        set_do_not_push_to_historystack();
    }
}

TAddRemoveCommand::TAddRemoveCommand(TContextItem* parent,
                                     TContextItem* item,
                                     bool historable,
                                     TransactionMethod doMethod,
                                     TransactionMethod undoMethod,
                                     const QString& des)
    : TCommand(parent, des)
    , m_parentItem(item)
    , m_doMethod([doMethod]() { TRtTransaction transaction; doMethod(transaction); })
    , m_undoMethod([undoMethod]() { TRtTransaction transaction; undoMethod(transaction); })
{
    if (!historable) {
        set_do_not_push_to_historystack();
    }
}

int TAddRemoveCommand::do_action()
{
    if (m_doMethod) {
        m_doMethod();
        return 1;
    }
    return -1;
}

int TAddRemoveCommand::undo_action()
{
    if (m_undoMethod) {
        m_undoMethod();
        return 1;
    }
    return -1;
}
