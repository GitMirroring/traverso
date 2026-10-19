// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of TRecordingCommand, the undo step of a recording session

#include "TRecordingCommand.h"

#include <QUndoStack>

#include <cstdio>

static int failures = 0;

static void check(bool ok, const char* what)
{
    if (!ok) {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}

// Stands for the step that added a batch of takes to their tracks
class Step : public QUndoCommand
{
public:
    explicit Step(QList<char>& log, char name) : m_log(log), m_name(name) {}
    void redo() override { m_log.append(m_name); }
    void undo() override { m_log.append(char(m_name - 'a' + 'A')); }

private:
    QList<char>& m_log;
    char m_name;
};

class Edit : public QUndoCommand
{
public:
    void redo() override {}
    void undo() override {}
};

struct Session {
    QList<char> log;
    QList<QList<TAudioClip*>> discarded;
};

// The command never looks at the takes: addresses stand for them
static int takeA = 0;
static int takeB = 0;
static TAudioClip* const TakeA = reinterpret_cast<TAudioClip*>(&takeA);
static TAudioClip* const TakeB = reinterpret_cast<TAudioClip*>(&takeB);

static TRecordingCommand* make_session(Session& session)
{
    std::vector<std::unique_ptr<QUndoCommand>> steps;
    steps.push_back(std::make_unique<Step>(session.log, 'a'));
    steps.push_back(std::make_unique<Step>(session.log, 'b'));
    return new TRecordingCommand([&session](const QList<TAudioClip*>& takes) { session.discarded.append(takes); },
                                 std::move(steps), {TakeA, TakeB}, QStringLiteral("Recording"));
}

int main()
{
    {
        // Pushed after the recording: the takes are there, the first redo
        // runs nothing; undo runs the steps backwards, redo forwards
        Session session;
        QUndoStack stack;
        stack.push(make_session(session));
        check(session.log.isEmpty(), "first redo does nothing");
        stack.undo();
        check(session.log == QList<char>({'B', 'A'}), "undo runs the steps backwards");
        stack.redo();
        check(session.log == QList<char>({'B', 'A', 'a', 'b'}), "redo runs them forwards");
        check(session.discarded.isEmpty(), "nothing discarded while on the stack");
    }
    {
        // Destroyed applied (the stack goes, the project closes): the takes stay
        Session session;
        {
            QUndoStack stack;
            stack.push(make_session(session));
        }
        check(session.discarded.isEmpty(), "applied: kept");
    }
    {
        // Undone, then a new edit drops the redo branch: discarded once
        Session session;
        QUndoStack stack;
        stack.push(make_session(session));
        stack.undo();
        stack.push(new Edit);
        check(session.discarded.size() == 1 && session.discarded.first() == QList<TAudioClip*>({TakeA, TakeB}),
              "dropped undone: its takes discarded");
    }
    {
        // Handled at project close: left alone when destroyed undone
        Session session;
        auto* stack = new QUndoStack;
        stack->push(make_session(session));
        stack->undo();
        const_cast<TRecordingCommand*>(static_cast<const TRecordingCommand*>(stack->command(0)))->set_handled();
        delete stack;
        check(session.discarded.isEmpty(), "handled: not discarded again");
    }
    {
        // The explicit discard: the last step, nothing undone after it
        Session session;
        QUndoStack stack;
        check(TRecordingCommand::last_discardable(stack) == nullptr, "empty stack: nothing");
        stack.push(make_session(session));
        TRecordingCommand* last = TRecordingCommand::last_discardable(stack);
        check(last != nullptr, "the recording is last");
        stack.push(new Edit);
        check(TRecordingCommand::last_discardable(stack) == nullptr, "an edit after it: nothing");
        stack.undo();
        check(TRecordingCommand::last_discardable(stack) == nullptr, "an undone edit after it: nothing");
    }
    {
        // Discarding: undone first, then deleted by the stack, its takes discarded
        Session session;
        QUndoStack stack;
        stack.push(new Edit);
        stack.push(make_session(session));
        TRecordingCommand* last = TRecordingCommand::last_discardable(stack);
        check(last != nullptr, "discardable");
        if (last) {
            last->discard_on_undo();
            stack.undo();
        }
        check(session.log == QList<char>({'B', 'A'}), "undone before it goes");
        check(stack.count() == 1 && stack.index() == 1, "the stack deleted it, the edit before it stays");
        check(session.discarded.size() == 1, "its takes discarded");
        check(!stack.canRedo(), "nothing to redo");
    }

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all checks passed\n");
    return 0;
}
