// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

/**
 * @brief Background work of a TIOScheduler, such as a peak build
 * (doc/design/diskio-rounds.md).
 *
 * The scheduler runs one step at a time of a task, on a worker thread of its
 * pool, next to the streams' steps and other tasks' steps; the next step may
 * run on another thread (the scheduler's locks order them). A step is about
 * one read of a reader; steps may take different times (a WAV and a WavPack
 * file): none waits for another.
 */
class TIOIdleTask
{
public:
    virtual ~TIOIdleTask() = default;
    // One unit of work, about one reader read; false when the task is over
    // (the scheduler hands it back and deletes it then)
    virtual bool step() = 0;
    // The scheduler hands the task back right before it deletes it: when it
    // is over (on the scheduler's thread, after its last step), or when the
    // scheduler goes first (on the thread that deletes it). Reporting a result belongs
    // here, not in the destructor.
    virtual void retire() {}
};
