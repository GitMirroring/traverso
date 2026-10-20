// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <vector>

/**
 * @brief Decides what a round of a DiskIO thread does: which streams get a
 * step, in which order, how many background (idle) tasks join them, and
 * whether the next round follows at once (doc/design/diskio-rounds.md).
 *
 * No time is measured and nothing is weighed: the kernel schedules, DiskIO
 * only knows the order. Streams come first, the emptiest first; idle tasks
 * fill the threads that are left, never more than the idle limit, and only
 * while the emptiest reader is full enough (with hysteresis, so idle work
 * does not flicker on and off around one threshold).
 *
 * Pure: no threads, no streams, no audio device, so it is tested headless.
 * TDiskIOThread asks it once per round, on its own thread.
 */
class TDiskIOPlanner
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
        // idle work away for good)
        bool countsForFill{false};
        int fill{100};      // percent of the ring that holds data
    };

    struct Round
    {
        std::vector<int> streams;       // indices into the streams, in order
        int idleSteps{0};               // the first idleSteps idle tasks join
        // The next round starts without waiting for the audio thread's
        // tick. Only with idle work: a stream step fills or flushes all it
        // can, so a stream that still wants work waits for the next tick
        // (and one that cannot make progress does not spin).
        bool continueWithoutTick{false};
    };

    // threads: how many steps can run at the same time (the pool's threads)
    void set_threads(int threads) { m_threads = threads > 0 ? threads : 1; }
    // At most this many idle tasks per round (1 on a disk that seeks)
    void set_idle_limit(int limit) { m_idleLimit = limit > 0 ? limit : 1; }
    int threads() const { return m_threads; }
    int idle_limit() const { return m_idleLimit; }

    // seek: every stream takes a step (they all seek) and no idle work
    // runs; the idle work waits until the readers are above the start
    // threshold again. idleTasks: the idle tasks that need work now.
    // idleAllowed: false while recording.
    Round plan(const std::vector<Stream>& streams, bool seek, int idleTasks, bool idleAllowed);

    // Whether idle work may run as far as the readers' fill goes
    bool idle_running() const { return m_idleRunning; }

private:
    int m_threads{1};
    int m_idleLimit{1};
    bool m_idleRunning{false};
};
