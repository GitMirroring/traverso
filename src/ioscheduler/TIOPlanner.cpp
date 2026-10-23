// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TIOPlanner.h"

#include <algorithm>
#include <numeric>

TIOPlanner::Plan TIOPlanner::plan(const std::vector<TIOStream::State>& streams, bool playingSeek, const std::vector<qint64>& idleTasks,
                                   bool idleAllowed, int freeThreads, int idleBudget)
{
    Plan plan;
    freeThreads = std::max(freeThreads, 0);

    // The emptiest reader of the songs that play; none: nothing holds idle
    // work back
    int lowest = 100;
    std::vector<int> refills;
    for (int i = 0; i < int(streams.size()); ++i) {
        const TIOStream::State& stream = streams[size_t(i)];
        // A stream that seeks takes no refill: the seek fills it
        if (stream.seekPending) {
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

    // The heaviest idle tasks first: the longest one does not start last
    // and run alone at the end; equal weights in the order they came
    const size_t idleSteps = size_t(std::max(budget - int(plan.idleSeeks.size()), 0));
    plan.idleTasks.resize(idleTasks.size());
    std::iota(plan.idleTasks.begin(), plan.idleTasks.end(), 0);
    std::stable_sort(plan.idleTasks.begin(), plan.idleTasks.end(), [&idleTasks](int a, int b) {
        return idleTasks[size_t(a)] > idleTasks[size_t(b)];
    });
    plan.idleTasks.resize(std::min(plan.idleTasks.size(), idleSteps));
    return plan;
}
