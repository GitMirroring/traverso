// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of TDiskIOPlanner, the decisions of a DiskIO round

#include "TDiskIOPlanner.h"

#include <cstdio>
#include <vector>

static int failures = 0;

static void check(bool ok, const char* what)
{
    if (!ok) {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}

using Stream = TDiskIOPlanner::Stream;

// A reader as DiskIO reports it: work below the threshold
static Stream reader(int fill)
{
    return Stream{fill <= TDiskIOPlanner::IdleStartThreshold, true, fill};
}

int main()
{
    {
        // Order: the reader under the threshold, then as many idle tasks as
        // there are threads left; full readers get no step
        TDiskIOPlanner planner;
        planner.set_threads(2);
        planner.set_idle_limit(2);
        planner.plan({reader(100), reader(90), reader(95)}, false, 4, true);   // idle work starts
        const TDiskIOPlanner::Round round = planner.plan({reader(100), reader(75), reader(95)}, false, 4, true);
        check(round.streams == std::vector<int>({1}), "only the reader under the threshold");
        check(round.idleSteps == 1, "idle tasks fill the threads left");
    }
    {
        // The emptiest reader first
        TDiskIOPlanner planner;
        planner.set_threads(8);
        const TDiskIOPlanner::Round round = planner.plan({reader(70), reader(20), reader(50)}, false, 0, true);
        check(round.streams == std::vector<int>({1, 2, 0}), "lowest fill first");
    }
    {
        // Hysteresis: starts above 80, goes on down to 60, starts again only
        // above 80
        TDiskIOPlanner planner;
        planner.set_threads(4);
        planner.set_idle_limit(4);
        check(planner.plan({reader(75)}, false, 2, true).idleSteps == 0, "75 does not start idle work");
        check(planner.plan({reader(85)}, false, 2, true).idleSteps == 2, "85 starts it");
        check(planner.plan({reader(75)}, false, 2, true).idleSteps > 0, "75 goes on");
        check(planner.plan({reader(60)}, false, 2, true).idleSteps > 0, "60 goes on");
        check(planner.plan({reader(55)}, false, 2, true).idleSteps == 0, "55 stops it");
        check(planner.plan({reader(75)}, false, 2, true).idleSteps == 0, "75 does not start it again");
        check(planner.plan({reader(80)}, false, 2, true).idleSteps == 0, "80 is not above the start threshold");
        check(planner.plan({reader(81)}, false, 2, true).idleSteps > 0, "81 starts it again");
    }
    {
        // A reader out of its window does not count, also not with an empty
        // ring, and when the transport came close it gets its step
        TDiskIOPlanner planner;
        planner.set_threads(4);
        planner.set_idle_limit(4);
        const Stream away{false, false, 0};
        check(planner.plan({reader(100), away}, false, 1, true).idleSteps == 1, "out of the window: no reason to wait");
        const Stream entered{true, false, 0};
        const TDiskIOPlanner::Round round = planner.plan({reader(100), entered}, false, 1, true);
        check(round.streams == std::vector<int>({1}), "a reader whose window the transport entered gets a step");
        check(round.idleSteps == 1, "and does not hold the idle work back");
    }
    {
        // Seek: every stream seeks, no idle work, and none until the readers
        // are above the start threshold again
        TDiskIOPlanner planner;
        planner.set_threads(4);
        planner.set_idle_limit(4);
        planner.plan({reader(100), reader(100)}, false, 2, true);
        check(planner.idle_running(), "idle work running before the seek");
        const TDiskIOPlanner::Round seek = planner.plan({reader(100), reader(100)}, true, 2, true);
        check(seek.streams.size() == 2 && seek.idleSteps == 0, "a seek round: all streams, no idle work");
        check(planner.plan({reader(70), reader(70)}, false, 2, true).idleSteps == 0, "no idle work after the seek at 70");
        check(planner.plan({reader(100), reader(90)}, false, 2, true).idleSteps > 0, "back above 80: idle work again");
    }
    {
        // Idle limit 1: never more than one idle task, also with free threads
        TDiskIOPlanner planner;
        planner.set_threads(8);
        planner.set_idle_limit(1);
        check(planner.plan({}, false, 4, true).idleSteps == 1, "limit 1");
        planner.set_idle_limit(8);
        check(planner.plan({}, false, 3, true).idleSteps == 3, "no more than there are");
    }
    {
        // Recording: no idle work at all
        TDiskIOPlanner planner;
        planner.set_threads(8);
        planner.set_idle_limit(8);
        check(planner.plan({reader(100)}, false, 4, false).idleSteps == 0, "not while recording");
    }
    {
        // The next round without waiting only with idle work; streams wait
        // for the next tick
        TDiskIOPlanner planner;
        planner.set_threads(4);
        planner.set_idle_limit(4);
        check(planner.plan({reader(100)}, false, 1, true).continueWithoutTick, "idle work: go on");
        const TDiskIOPlanner::Round streams = planner.plan({reader(50)}, false, 0, true);
        check(!streams.continueWithoutTick && streams.streams.size() == 1, "only streams: wait for the tick");
        const TDiskIOPlanner::Round nothing = planner.plan({reader(100)}, false, 0, true);
        check(nothing.streams.empty() && nothing.idleSteps == 0 && !nothing.continueWithoutTick, "nothing: wait");
    }
    {
        // Writers: only those with something to write, no fill rule
        TDiskIOPlanner planner;
        planner.set_threads(2);
        const Stream full{true, false, 30};
        const Stream empty{false, false, 100};
        const TDiskIOPlanner::Round round = planner.plan({empty, full, empty}, false, 0, false);
        check(round.streams == std::vector<int>({1}) && round.idleSteps == 0, "writers with data only");
    }

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all checks passed\n");
    return 0;
}
