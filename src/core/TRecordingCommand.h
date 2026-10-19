// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QList>
#include <QUndoCommand>

#include <functional>
#include <memory>
#include <vector>

class QUndoStack;
class TAudioClip;

/**
 * @brief The undo step of a recording session: all its takes, on all armed
 * tracks, the takes of a seek during the recording included
 * (doc/design/recording-session.md).
 *
 * The takes are in their tracks already when it is pushed (they appear as
 * they are recorded), so its first redo does nothing; later ones run the
 * steps that added them again. When it is destroyed undone (a new edit
 * dropped it, the explicit discard), its takes can never come back: it
 * hands them to the discard function (the sheet's queues them to
 * TResourcesManager::discard_takes(), since the undo stack is in the middle
 * of a push or an undo then). A session handled at project close is left
 * alone (set_handled()). The explicit discard undoes it with
 * discard_on_undo(): it makes itself obsolete in that undo, and the stack
 * deletes an obsolete command after undoing it.
 */
class TRecordingCommand : public QUndoCommand
{
public:
    using Discard = std::function<void(const QList<TAudioClip*>& takes)>;

    TRecordingCommand(Discard discard, std::vector<std::unique_ptr<QUndoCommand>> steps,
                      QList<TAudioClip*> takes, const QString& text);
    ~TRecordingCommand() override;

    void redo() override;
    void undo() override;

    bool is_applied() const { return m_applied; }
    const QList<TAudioClip*>& takes() const { return m_takes; }
    void set_handled() { m_handled = true; }
    void discard_on_undo() { m_discardOnUndo = true; }

    // The recording the explicit discard may drop: the last step of the
    // stack, with nothing undone after it. (With a redo branch, undone
    // edits could bring its takes back after they are gone.) The stack only
    // hands out const commands; changing the session's own flags leaves the
    // stack as it is.
    static TRecordingCommand* last_discardable(const QUndoStack& stack);

private:
    Discard m_discard;
    std::vector<std::unique_ptr<QUndoCommand>> m_steps;
    QList<TAudioClip*> m_takes;
    bool m_applied{true};
    bool m_firstRedo{true};
    bool m_handled{false};
    bool m_discardOnUndo{false};
};
