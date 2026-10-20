// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TDiskIOPlanner.h"

#include <algorithm>

TDiskIOPlanner::Round TDiskIOPlanner::plan(const std::vector<Stream>& streams, bool seek, int idleTasks, bool idleAllowed,
                                           int threads, int idleLimit)
{
    Round round;

    // The emptiest reader that counts; none: nothing holds idle work back
    int lowest = 100;
    for (const Stream& stream : streams) {
        if (stream.countsForFill) {
            lowest = std::min(lowest, stream.fill);
        }
    }

    for (int i = 0; i < int(streams.size()); ++i) {
        if (seek || streams[size_t(i)].needsWork) {
            round.streams.push_back(i);
        }
    }
    // The emptiest first: blockingMap starts at the front
    std::stable_sort(round.streams.begin(), round.streams.end(), [&streams](int a, int b) {
        return streams[size_t(a)].fill < streams[size_t(b)].fill;
    });

    if (seek) {
        m_idleRunning = false;
        return round;
    }

    m_idleRunning = m_idleRunning ? lowest >= ResumeThreshold : lowest > IdleStartThreshold;
    if (m_idleRunning && idleAllowed && idleTasks > 0) {
        const int free = std::max(threads, 1) - int(round.streams.size());
        round.idleSteps = std::clamp(free, 0, std::min(std::max(idleLimit, 1), idleTasks));
    }
    round.continueWithoutTick = round.idleSteps > 0;
    return round;
}
