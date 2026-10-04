/*
    Copyright (C) 2005-2026 Remon Sijrier
    This file is part of Traverso
*/

#include "CommandGroup.h"
#include "TAudioThreadMessageQueue.h"
#include "Debugger.h"

CommandGroup::~CommandGroup()
{
    for (TCommand* cmd : m_commands) {
        delete cmd;
    }
}

int CommandGroup::prepare_actions()
{
    if (m_commands.empty()) {
        return -1;
    }

    int result = 1;
    for (TCommand* cmd : m_commands) {
        if (cmd->prepare_actions() == -1) {
            PWARN("CommandGroup::prepare_actions: one of the commands in the group failed prepare_actions\n");
            result = -1;
        }
    }

    return result;
}

int CommandGroup::do_action()
{
    // 1. Audio thread triggers the real-time batch transaction start boundaries.
    // 2. GUI thread receives the asynchronous started notification return signal.
    tsmp().post_rt_task(
        []() { tsmp().trigger_batch_start(); },
        []() { emit tsmp().batchTransactionStarted(); }
        );

    // Cascade execution down to all registered child commands locally inside this context block
    for (TCommand* cmd : m_commands) {
        cmd->do_action();
    }

    // 1. Audio thread triggers the real-time batch transaction finish boundaries.
    // 2. GUI thread receives the asynchronous finished notification return signal.
    tsmp().post_rt_task(
        []() { tsmp().trigger_batch_finish(); },
        []() { emit tsmp().batchTransactionFinished(); }
        );

    return 1;
}

/**
 * @brief Executes the symmetric backward historical undo macro batch transaction pipeline.
 */
int CommandGroup::undo_action()
{
    // MODERN CLOSURE DISPATCH: Symmetric boundary routing for historical undo operations
    tsmp().post_rt_task(
        []() { tsmp().trigger_batch_start(); },
        []() { emit tsmp().batchTransactionStarted(); }
        );

    // Roll back states in strict reverse chronological order across child components
    for (auto it = m_commands.rbegin(); it != m_commands.rend(); ++it) {
        (*it)->undo_action();
    }

    tsmp().post_rt_task(
        []() { tsmp().trigger_batch_finish(); },
        []() { emit tsmp().batchTransactionFinished(); }
        );

    return 1;
}

