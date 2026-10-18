// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TRecordingCommand.h"

#include "TResourcesManager.h"

#include <QMetaObject>

TRecordingCommand::TRecordingCommand(TResourcesManager* resources, std::vector<std::unique_ptr<QUndoCommand>> steps,
                                     QList<TAudioClip*> takes, const QString& text)
    : QUndoCommand(text)
    , m_resources(resources)
    , m_steps(std::move(steps))
    , m_takes(std::move(takes))
{
}

TRecordingCommand::~TRecordingCommand()
{
    if (m_applied || m_handled || m_takes.isEmpty() || !m_resources) {
        return;
    }
    // Dropped from the stack while undone: the takes can never come back.
    // Queued on the resources manager, so the call is dropped if the
    // project goes first.
    TResourcesManager* resources = m_resources;
    QMetaObject::invokeMethod(resources, [resources, takes = m_takes] { resources->discard_takes(takes); }, Qt::QueuedConnection);
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
