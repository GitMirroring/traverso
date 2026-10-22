// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TIOPlanner.h"

#include <algorithm>

TIOPlanner::Round TIOPlanner::plan(const std::vector<Stream>& streams, int idleTasks, bool idleAllowed, int threads,
                                   int idleLimit)
{
    Round round;

    // The emptiest reader of the songs that play; none: nothing holds idle
    // work back
    int lowest = 100;
    bool playingSeek = false;
    std::vector<int> refills;
    for (int i = 0; i < int(streams.size()); ++i) {
        const Stream& stream = streams[size_t(i)];
        // A stream that seeks takes no refill: the seek fills it
        if (stream.seek) {
            if (stream.playing) {
                round.streams.push_back(i);
                playingSeek = true;
            } else {
                round.idleSeeks.push_back(i);
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
    // The emptiest first: blockingMap starts at the front
    std::stable_sort(refills.begin(), refills.end(), [&streams](int a, int b) {
        return streams[size_t(a)].fill < streams[size_t(b)].fill;
    });
    round.streams.insert(round.streams.end(), refills.begin(), refills.end());

    if (playingSeek) {
        m_idleRunning = false;
        round.idleSeeks.clear();
        return round;
    }

    m_idleRunning = m_idleRunning ? lowest >= ResumeThreshold : lowest > IdleStartThreshold;
    int budget = 0;
    if (m_idleRunning && idleAllowed) {
        const int free = std::max(threads, 1) - int(round.streams.size());
        budget = std::clamp(free, 0, std::max(idleLimit, 1));
    }
    round.idleSeeks.resize(std::min(round.idleSeeks.size(), size_t(budget)));
    round.idleSteps = std::clamp(budget - int(round.idleSeeks.size()), 0, idleTasks);
    round.continueWithoutTick = !round.idleSeeks.empty() || round.idleSteps > 0;
    return round;
}
