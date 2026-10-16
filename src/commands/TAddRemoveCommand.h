/*
    Copyright (C) 2005-2026 Remon Sijrier
    This file is part of Traverso
*/

#pragma once

#include "TCommand.h"
#include <functional>

class TContextItem;

/**
 * @brief An undoable command from a do and an undo function.
 *
 * Both run directly in the GUI thread. Real-time state is changed with the
 * transaction variant, in which each function gets the TRtTransaction to use.
 * Goes away when commands become controllers (branch 3), TUndoCommand is the
 * replacement.
 */
class TAddRemoveCommand : public TCommand
{
    Q_OBJECT

public:
    explicit TAddRemoveCommand(TContextItem* parent,
                               bool historable,
                               std::function<void()> doMethod,
                               std::function<void()> undoMethod,
                               const QString& des);

    // For owners whose lists are TRtLists: the methods change them in the GUI
    // thread with the transaction they are given.
    using TransactionMethod = std::function<void(TRtTransaction& transaction)>;

    TAddRemoveCommand(TContextItem* parent,
                      TContextItem* item,
                      bool historable,
                      TransactionMethod doMethod,
                      TransactionMethod undoMethod,
                      const QString& des);

    ~TAddRemoveCommand() override = default;

    bool is_hold_command() const override { return false; }
    int prepare_actions() override { return 1; }

    int do_action() override;
    int undo_action() override;

private:
    TContextItem*         m_parentItem{nullptr};
    std::function<void()> m_doMethod;
    std::function<void()> m_undoMethod;
};
