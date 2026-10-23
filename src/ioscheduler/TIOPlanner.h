// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "TIOStream.h"

#include <vector>

/**
 * @brief Decides what a TIOScheduler starts when threads are free: which of
 * the streams and idle tasks that wait get a step, in which order
 * (doc/design/diskio-rounds.md, dispatcher-conclusie.md). What does not fit
 * waits for the next time a thread is free.
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
 * TIOScheduler asks it on its own thread, every time it dispatches.
 */
class TIOPlanner
{
public:
    // Idle work starts when the emptiest reader is above this (percent of
    // its ring): the readers can go a while without a step then. A reader
    // gets a step at this fill or below.
    static constexpr int IdleStartThreshold = 80;
    // Once running, idle work goes on until the emptiest reader drops below
    // this: a reader that slips below the start threshold gets its step
    // anyway, and stopping at once would make the idle work stop and start
    // all the time around one threshold.
    static constexpr int IdleStopThreshold = 60;

    struct Plan
    {
        // Indices into the streams, in order: the seeks of songs that play,
        // then the refills, the emptiest first; no more than there are threads
        std::vector<int> streams;
        // The seeks of songs that do not play, as idle work, in order
        std::vector<int> idleSeeks;
        int idleSteps{0};               // the first idleSteps idle tasks get a step
    };

    // streams: those that do not run a step now. playingSeek: a song that
    // plays seeks (also when all its seek steps run): no idle work starts,
    // and none until the readers are above the start threshold again; a
    // stream that seeks for a song that plays says so too. idleTasks: the
    // idle tasks that wait. idleAllowed: false while a song records.
    // freeThreads: how many steps may start now (the threads that are
    // free). idleBudget: how many idle steps may start now.
    Plan plan(const std::vector<TIOStream::State>& streams, bool playingSeek, int idleTasks, bool idleAllowed, int freeThreads, int idleBudget);

    // Whether idle work may run as far as the readers' fill goes
    bool idle_running() const { return m_idleRunning; }

private:
    bool m_idleRunning{false};
};
