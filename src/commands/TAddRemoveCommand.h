/*
    Copyright (C) 2005-2026 Remon Sijrier
    This file is part of Traverso
*/

#pragma once

#include "TCommand.h"
#include <functional>

class TSession;
class TContextItem;

class TAddRemoveCommand : public TCommand
{
    Q_OBJECT

public:
    TAddRemoveCommand(TContextItem* parent,
                      void* arg,
                      bool historable,
                      TSession* sheet,
                      std::move_only_function<void()>&& doMethod,   std::move_only_function<void()>&& doSignal,
                      std::move_only_function<void()>&& undoMethod, std::move_only_function<void()>&& undoSignal,
                      const QString& des);

    TAddRemoveCommand(TContextItem* parent,
                      TContextItem* item,
                      bool historable,
                      TSession* sheet,
                      std::move_only_function<void()>&& doMethod,   std::move_only_function<void()>&& doSignal,
                      std::move_only_function<void()>&& undoMethod, std::move_only_function<void()>&& undoSignal,
                      const QString& des);

    virtual ~TAddRemoveCommand() override = default;

    bool is_hold_command() const override { return false; }
    int prepare_actions() override { return 1; }

    int do_action() override;
    int undo_action() override;

    void set_instantanious(bool instant);

private:
    int un_redo_action(std::move_only_function<void()>& method, std::move_only_function<void()>& signal);

    TContextItem*                   m_parentItem{nullptr};
    void*                           m_arg{nullptr};
    TSession*                       m_sheet{nullptr};
    std::move_only_function<void()> m_doMethod;
    std::move_only_function<void()> m_undoMethod;
    std::move_only_function<void()> m_doSignal;
    std::move_only_function<void()> m_undoSignal;
    bool                            m_instantanious{false};
};

