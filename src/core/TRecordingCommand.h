// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QList>
#include <QPointer>
#include <QUndoCommand>

#include <memory>
#include <vector>

class TAudioClip;
class TResourcesManager;

/**
 * @brief The undo step of a recording session: all its takes, on all armed
 * tracks, the takes of a seek during the recording included
 * (doc/design/recording-session.md).
 *
 * The takes are in their tracks already when it is pushed (they appear as
 * they are recorded), so its first redo does nothing; later ones run the
 * steps that added them again. When it is destroyed undone (a new edit
 * dropped it, the explicit discard), its takes can never come back: it
 * hands them to TResourcesManager::discard_takes(), queued, since the undo
 * stack is in the middle of a push or an undo then. A session handled at
 * project close is left alone (set_handled()). The explicit discard undoes
 * it with discard_on_undo(): it makes itself obsolete in that undo, and the
 * stack deletes an obsolete command after undoing it.
 */
class TRecordingCommand : public QUndoCommand
{
public:
    TRecordingCommand(TResourcesManager* resources, std::vector<std::unique_ptr<QUndoCommand>> steps,
                      QList<TAudioClip*> takes, const QString& text);
    ~TRecordingCommand() override;

    void redo() override;
    void undo() override;

    bool is_applied() const { return m_applied; }
    const QList<TAudioClip*>& takes() const { return m_takes; }
    void set_handled() { m_handled = true; }
    void discard_on_undo() { m_discardOnUndo = true; }

private:
    QPointer<TResourcesManager> m_resources;
    std::vector<std::unique_ptr<QUndoCommand>> m_steps;
    QList<TAudioClip*> m_takes;
    bool m_applied{true};
    bool m_firstRedo{true};
    bool m_handled{false};
    bool m_discardOnUndo{false};
};
