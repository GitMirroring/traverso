// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <vector>

/**
 * @brief Decides what a round of a TIOScheduler does: which streams get a
 * step, in which order, how many background (idle) tasks join them, and
 * whether the next round follows at once (doc/design/diskio-rounds.md,
 * dispatcher-conclusie.md).
 *
 * No time is measured and nothing is weighed: the kernel schedules, the scheduler
 * only knows the order:
 * 1. the seeks of songs that play (or play once the seek is done);
 * 2. refills, the emptiest first;
 * 3. idle work on the threads that are left, never more than the idle
 *    limit: first the seeks of songs that do not play (a preparation to
 *    play), then the idle tasks. Only while the emptiest reader of the songs
 *    that play is full enough (with hysteresis, so idle work does not
 *    flicker on and off around one threshold), and not while a song that
 *    plays seeks.
 *
 * Pure: no threads, no streams, no audio device, so it is tested headless.
 * TIOScheduler asks it once per round, on its own thread.
 */
class TIOPlanner
{
public:
    // Idle work starts when the emptiest reader is above this (percent of
    // its ring): the readers can take a round without a step then. It was
    // the threshold a reader gets a step below before the round loop too.
    static constexpr int IdleStartThreshold = 80;
    // Once running, idle work goes on until the emptiest reader drops below
    // this: a reader that slips below the start threshold gets its step in
    // the same round anyway, and stopping at once would make the idle work
    // stop and start with every round around one threshold.
    static constexpr int ResumeThreshold = 60;

    struct Stream
    {
        bool needsWork{false};
        // A reader whose fill decides about idle work: not a writer, not a
        // reader out of its window (nothing to read there, it would keep the
        // idle work away for good). Only the readers of songs that play count.
        bool countsForFill{false};
        int fill{100};      // percent of the ring that holds data
        // Its seek step is to come (its song seeks)
        bool seek{false};
        // Its song plays, or plays once its seek is done; a writer has no song
        bool playing{false};
    };

    struct Round
    {
        // Indices into the streams, in order: the seeks of songs that play,
        // then the refills, the emptiest first
        std::vector<int> streams;
        // The seeks of songs that do not play, as idle work, in order
        std::vector<int> idleSeeks;
        int idleSteps{0};               // the first idleSteps idle tasks join
        // The next round starts without waiting for the audio thread's
        // tick. Only with idle work: a stream step fills or flushes all it
        // can, so a stream that still wants work waits for the next tick
        // (and one that cannot make progress does not spin).
        bool continueWithoutTick{false};
    };

    // A seek of a song that plays: no idle work runs, and it waits until the
    // readers are above the start threshold again. idleTasks: the idle tasks
    // that need work now. idleAllowed: false while a song records. threads:
    // how many steps can run at the same time (the pool's threads).
    // idleLimit: at most this many idle steps per round (1 on a disk that
    // seeks).
    Round plan(const std::vector<Stream>& streams, int idleTasks, bool idleAllowed, int threads, int idleLimit);

    // Whether idle work may run as far as the readers' fill goes
    bool idle_running() const { return m_idleRunning; }

private:
    bool m_idleRunning{false};
};
