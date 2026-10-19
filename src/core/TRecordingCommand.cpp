// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TRecordingCommand.h"

#include <QUndoStack>

TRecordingCommand::TRecordingCommand(Discard discard, std::vector<std::unique_ptr<QUndoCommand>> steps,
                                     QList<TAudioClip*> takes, const QString& text)
    : QUndoCommand(text)
    , m_discard(std::move(discard))
    , m_steps(std::move(steps))
    , m_takes(std::move(takes))
{
}

TRecordingCommand::~TRecordingCommand()
{
    if (m_applied || m_handled || m_takes.isEmpty() || !m_discard) {
        return;
    }
    // Dropped from the stack while undone: the takes can never come back
    m_discard(m_takes);
}

void TRecordingCommand::redo()
{
    if (m_firstRedo) {
        // Pushed after the recording: the takes are in their tracks already
        m_firstRedo = false;
        return;
    }
    for (const std::unique_ptr<QUndoCommand>& step : m_steps) {
        step->redo();
    }
    m_applied = true;
}

void TRecordingCommand::undo()
{
    for (auto step = m_steps.rbegin(); step != m_steps.rend(); ++step) {
        (*step)->undo();
    }
    m_applied = false;
    if (m_discardOnUndo) {
        setObsolete(true);
    }
}

TRecordingCommand* TRecordingCommand::last_discardable(const QUndoStack& stack)
{
    if (stack.index() == 0 || stack.index() != stack.count()) {
        return nullptr;
    }
    auto* last = dynamic_cast<const TRecordingCommand*>(stack.command(stack.index() - 1));
    return const_cast<TRecordingCommand*>(last);
}
