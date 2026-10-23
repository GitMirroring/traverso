// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TIOPlanner.h"

#include <algorithm>

TIOPlanner::Plan TIOPlanner::plan(const std::vector<Stream>& streams, bool playingSeek, int idleTasks, bool idleAllowed,
                                   int freeThreads, int idleBudget)
{
    Plan plan;
    freeThreads = std::max(freeThreads, 0);

    // The emptiest reader of the songs that play; none: nothing holds idle
    // work back
    int lowest = 100;
    std::vector<int> refills;
    for (int i = 0; i < int(streams.size()); ++i) {
        const Stream& stream = streams[size_t(i)];
        // A stream that seeks takes no refill: the seek fills it
        if (stream.seek) {
            if (stream.playing) {
                plan.streams.push_back(i);
                playingSeek = true;
            } else {
                plan.idleSeeks.push_back(i);
            }
            continue;
        }
        if (stream.countsForFill && stream.playing) {
            lowest = std::min(lowest, stream.fill);
        }
        if (stream.needsWork) {
            refills.push_back(i);
        }
    }
    // The emptiest first
    std::stable_sort(refills.begin(), refills.end(), [&streams](int a, int b) {
        return streams[size_t(a)].fill < streams[size_t(b)].fill;
    });
    plan.streams.insert(plan.streams.end(), refills.begin(), refills.end());
    // What there are no free threads for waits
    plan.streams.resize(std::min(plan.streams.size(), size_t(freeThreads)));

    if (playingSeek) {
        m_idleRunning = false;
        plan.idleSeeks.clear();
        return plan;
    }

    m_idleRunning = m_idleRunning ? lowest >= IdleStopThreshold : lowest > IdleStartThreshold;
    int budget = 0;
    if (m_idleRunning && idleAllowed) {
        budget = std::clamp(freeThreads - int(plan.streams.size()), 0, std::max(idleBudget, 0));
    }
    plan.idleSeeks.resize(std::min(plan.idleSeeks.size(), size_t(budget)));
    plan.idleSteps = std::clamp(budget - int(plan.idleSeeks.size()), 0, std::max(idleTasks, 0));
    return plan;
}
