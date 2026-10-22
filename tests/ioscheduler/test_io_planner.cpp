// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of TIOPlanner, the decisions of a scheduler round

#include "TIOPlanner.h"

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

using Stream = TIOPlanner::Stream;

// A reader of a song that plays, as the scheduler reports it: work below
// the threshold
static Stream reader(int fill)
{
    return Stream{fill <= TIOPlanner::IdleStartThreshold, true, fill, false, true};
}

// A reader of a song that does not play
static Stream stopped(int fill)
{
    return Stream{fill <= TIOPlanner::IdleStartThreshold, true, fill, false, false};
}

// A reader whose song seeks
static Stream seeking(bool playing)
{
    return Stream{false, true, 100, true, playing};
}

int main()
{
    {
        // Order: the reader under the threshold, then as many idle tasks as
        // there are threads left; full readers get no step
        TIOPlanner planner;
        planner.plan({reader(100), reader(90), reader(95)}, false, 4, true, 2, 2);   // idle work starts
        const TIOPlanner::Plan round = planner.plan({reader(100), reader(75), reader(95)}, false, 4, true, 2, 2);
        check(round.streams == std::vector<int>({1}), "only the reader under the threshold");
        check(round.idleSteps == 1, "idle tasks fill the threads left");
    }
    {
        // The emptiest reader first
        TIOPlanner planner;
        const TIOPlanner::Plan round = planner.plan({reader(70), reader(20), reader(50)}, false, 0, true, 8, 1);
        check(round.streams == std::vector<int>({1, 2, 0}), "lowest fill first");
    }
    {
        // Hysteresis: starts above 80, goes on down to 60, starts again only
        // above 80
        TIOPlanner planner;
        check(planner.plan({reader(75)}, false, 2, true, 4, 4).idleSteps == 0, "75 does not start idle work");
        check(planner.plan({reader(85)}, false, 2, true, 4, 4).idleSteps == 2, "85 starts it");
        check(planner.plan({reader(75)}, false, 2, true, 4, 4).idleSteps > 0, "75 goes on");
        check(planner.plan({reader(60)}, false, 2, true, 4, 4).idleSteps > 0, "60 goes on");
        check(planner.plan({reader(55)}, false, 2, true, 4, 4).idleSteps == 0, "55 stops it");
        check(planner.plan({reader(75)}, false, 2, true, 4, 4).idleSteps == 0, "75 does not start it again");
        check(planner.plan({reader(80)}, false, 2, true, 4, 4).idleSteps == 0, "80 is not above the start threshold");
        check(planner.plan({reader(81)}, false, 2, true, 4, 4).idleSteps > 0, "81 starts it again");
    }
    {
        // A reader out of its window does not count, also not with an empty
        // ring, and when the transport came close it gets its step
        TIOPlanner planner;
        const Stream away{false, false, 0};
        check(planner.plan({reader(100), away}, false, 1, true, 4, 4).idleSteps == 1, "out of the window: no reason to wait");
        const Stream entered{true, false, 0};
        const TIOPlanner::Plan round = planner.plan({reader(100), entered}, false, 1, true, 4, 4);
        check(round.streams == std::vector<int>({1}), "a reader whose window the transport entered gets a step");
        check(round.idleSteps == 1, "and does not hold the idle work back");
    }
    {
        // A seek of a song that plays: its streams first, before the
        // refills, no idle work, and none until the readers are above the
        // start threshold again
        TIOPlanner planner;
        planner.plan({reader(100), reader(100)}, false, 2, true, 4, 4);
        check(planner.idle_running(), "idle work running before the seek");
        const TIOPlanner::Plan seek = planner.plan({reader(50), seeking(true), seeking(true)}, false, 2, true, 4, 4);
        check(seek.streams == std::vector<int>({1, 2, 0}) && seek.idleSteps == 0 && seek.idleSeeks.empty(),
              "the seeks of a song that plays before the refills, no idle work");
        check(planner.plan({reader(70), reader(70)}, false, 2, true, 4, 4).idleSteps == 0, "no idle work after the seek at 70");
        check(planner.plan({reader(100), reader(90)}, false, 2, true, 4, 4).idleSteps > 0, "back above 80: idle work again");
    }
    {
        // A seek of a song that does not play is idle work: after the
        // refills, before the idle tasks, within the idle limit and the
        // threads that are left
        TIOPlanner planner;
        const TIOPlanner::Plan round = planner.plan({stopped(50), seeking(false), seeking(false), seeking(false)}, false, 3, true, 4, 3);
        check(round.streams == std::vector<int>({0}), "the refill as a stream step");
        check(round.idleSeeks == std::vector<int>({1, 2, 3}) && round.idleSteps == 0, "idle seeks before the idle tasks, three of three");
        const TIOPlanner::Plan two = planner.plan({seeking(false), seeking(false)}, false, 3, true, 8, 4);
        check(two.idleSeeks.size() == 2 && two.idleSteps == 2, "the idle tasks take what the seeks leave");
        // A disk that seeks: one at a time
        check(planner.plan({seeking(false), seeking(false)}, false, 0, true, 8, 1).idleSeeks.size() == 1, "limit 1: one seek at a time");
        // Not while a song records
        check(planner.plan({seeking(false)}, false, 0, false, 8, 4).idleSeeks.empty(), "not while recording");
        // Behind the hysteresis of the songs that play
        TIOPlanner held;
        check(held.plan({reader(50), seeking(false)}, false, 0, true, 8, 4).idleSeeks.empty(), "held back by a song that plays");
        // A seek of a song that plays goes first, the idle seek waits
        TIOPlanner both;
        const TIOPlanner::Plan first = both.plan({seeking(false), seeking(true)}, false, 0, true, 8, 4);
        check(first.streams == std::vector<int>({1}) && first.idleSeeks.empty(), "the song that plays seeks first");
    }
    {
        // The readers of a song that does not play do not hold idle work back
        TIOPlanner planner;
        check(planner.plan({stopped(10), stopped(20)}, false, 2, true, 8, 4).idleSteps == 2, "songs that do not play: no reason to wait");
        check(planner.plan({stopped(10), reader(50)}, false, 2, true, 8, 4).idleSteps == 0, "a song that plays does");
    }
    {
        // Idle limit 1: never more than one idle task, also with free threads
        TIOPlanner planner;
        check(planner.plan({}, false, 4, true, 8, 1).idleSteps == 1, "limit 1");
        check(planner.plan({}, false, 3, true, 8, 8).idleSteps == 3, "no more than there are");
    }
    {
        // Recording: no idle work at all
        TIOPlanner planner;
        check(planner.plan({reader(100)}, false, 4, false, 8, 8).idleSteps == 0, "not while recording");
    }
    {
        // No more stream steps than there are threads free; what does not fit
        // waits, the emptiest first gets the thread
        TIOPlanner planner;
        const TIOPlanner::Plan two = planner.plan({reader(50), reader(10), reader(30)}, false, 4, true, 2, 4);
        check(two.streams == std::vector<int>({1, 2}) && two.idleSteps == 0, "two threads: the two emptiest, no idle work");
        check(planner.plan({reader(50)}, false, 4, true, 0, 4).streams.empty(), "no thread free: nothing starts");
        // A seek of a song that plays whose steps all run already still
        // holds idle work back
        TIOPlanner seeking;
        seeking.plan({reader(100)}, false, 2, true, 4, 4);
        check(seeking.plan({reader(100)}, true, 2, true, 4, 4).idleSteps == 0, "a song that plays seeks: no idle work");
        check(seeking.plan({reader(100)}, false, 2, true, 4, 4).idleSteps > 0, "after it, at 100: idle work again");
        // The idle budget is what may start now: 0 starts nothing
        check(planner.plan({}, false, 4, true, 8, 0).idleSteps == 0, "no idle budget left");
    }
    {
        // Writers: only those with something to write, no fill rule
        TIOPlanner planner;
        const Stream full{true, false, 30};
        const Stream empty{false, false, 100};
        const TIOPlanner::Plan round = planner.plan({empty, full, empty}, false, 0, false, 2, 1);
        check(round.streams == std::vector<int>({1}) && round.idleSteps == 0, "writers with data only");
    }

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all checks passed\n");
    return 0;
}
