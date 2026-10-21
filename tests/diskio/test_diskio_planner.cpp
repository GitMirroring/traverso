// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of TDiskIOPlanner, the decisions of a DiskIO round

#include "TBufferedAudioStreamStatus.h"
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
    // When DiskIO may do idle work (builds): no stream needs reading. A stream
    // far from the transport (out of its window) needs nothing, also though it
    // is not in sync for the audio thread
    {
        TBufferedAudioStreamStatus status;
        status.set_sync_status(TBufferedAudioStreamStatus::OUT_OF_WINDOW);
        status.set_fill_status(0);
        check(!status.needs_disk_work() && status.out_of_sync(), "a stream out of its window keeps DiskIO busy");
        status.set_sync_status(TBufferedAudioStreamStatus::OUT_OF_SYNC);
        status.set_fill_status(100);
        check(status.needs_disk_work(), "a stream out of sync near the transport needs no reading");
        status.set_sync_status(TBufferedAudioStreamStatus::IN_SYNC);
        status.set_fill_status(80);
        check(status.needs_disk_work(), "a stream with buffers at 80% needs no reading");
        status.set_fill_status(81);
        check(!status.needs_disk_work(), "a stream with full buffers needs reading");
        status.set_sync_status(TBufferedAudioStreamStatus::QUEUE_SEEKING_TO_NEW_LOCATION);
        check(status.needs_disk_work(), "a seeking stream needs no reading");
    }

    {
        // Order: the reader under the threshold, then as many idle tasks as
        // there are threads left; full readers get no step
        TDiskIOPlanner planner;
        planner.plan({reader(100), reader(90), reader(95)}, false, 4, true, 2, 2);   // idle work starts
        const TDiskIOPlanner::Round round = planner.plan({reader(100), reader(75), reader(95)}, false, 4, true, 2, 2);
        check(round.streams == std::vector<int>({1}), "only the reader under the threshold");
        check(round.idleSteps == 1, "idle tasks fill the threads left");
    }
    {
        // The emptiest reader first
        TDiskIOPlanner planner;
        const TDiskIOPlanner::Round round = planner.plan({reader(70), reader(20), reader(50)}, false, 0, true, 8, 1);
        check(round.streams == std::vector<int>({1, 2, 0}), "lowest fill first");
    }
    {
        // Hysteresis: starts above 80, goes on down to 60, starts again only
        // above 80
        TDiskIOPlanner planner;
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
        TDiskIOPlanner planner;
        const Stream away{false, false, 0};
        check(planner.plan({reader(100), away}, false, 1, true, 4, 4).idleSteps == 1, "out of the window: no reason to wait");
        const Stream entered{true, false, 0};
        const TDiskIOPlanner::Round round = planner.plan({reader(100), entered}, false, 1, true, 4, 4);
        check(round.streams == std::vector<int>({1}), "a reader whose window the transport entered gets a step");
        check(round.idleSteps == 1, "and does not hold the idle work back");
    }
    {
        // Seek: every stream seeks, no idle work, and none until the readers
        // are above the start threshold again
        TDiskIOPlanner planner;
        planner.plan({reader(100), reader(100)}, false, 2, true, 4, 4);
        check(planner.idle_running(), "idle work running before the seek");
        const TDiskIOPlanner::Round seek = planner.plan({reader(100), reader(100)}, true, 2, true, 4, 4);
        check(seek.streams.size() == 2 && seek.idleSteps == 0, "a seek round: all streams, no idle work");
        check(planner.plan({reader(70), reader(70)}, false, 2, true, 4, 4).idleSteps == 0, "no idle work after the seek at 70");
        check(planner.plan({reader(100), reader(90)}, false, 2, true, 4, 4).idleSteps > 0, "back above 80: idle work again");
    }
    {
        // Idle limit 1: never more than one idle task, also with free threads
        TDiskIOPlanner planner;
        check(planner.plan({}, false, 4, true, 8, 1).idleSteps == 1, "limit 1");
        check(planner.plan({}, false, 3, true, 8, 8).idleSteps == 3, "no more than there are");
    }
    {
        // Recording: no idle work at all
        TDiskIOPlanner planner;
        check(planner.plan({reader(100)}, false, 4, false, 8, 8).idleSteps == 0, "not while recording");
    }
    {
        // The next round without waiting only with idle work; streams wait
        // for the next tick
        TDiskIOPlanner planner;
        check(planner.plan({reader(100)}, false, 1, true, 4, 4).continueWithoutTick, "idle work: go on");
        const TDiskIOPlanner::Round streams = planner.plan({reader(50)}, false, 0, true, 4, 4);
        check(!streams.continueWithoutTick && streams.streams.size() == 1, "only streams: wait for the tick");
        const TDiskIOPlanner::Round nothing = planner.plan({reader(100)}, false, 0, true, 4, 4);
        check(nothing.streams.empty() && nothing.idleSteps == 0 && !nothing.continueWithoutTick, "nothing: wait");
        // Idle tasks that wait (behind the hysteresis, or while recording)
        // are no reason to go on: the thread would spin
        check(!planner.plan({reader(50)}, false, 2, true, 4, 4).continueWithoutTick, "idle work held back: wait for the tick");
        check(!planner.plan({reader(100)}, false, 2, false, 4, 4).continueWithoutTick, "recording: wait for the tick");
    }
    {
        // Writers: only those with something to write, no fill rule
        TDiskIOPlanner planner;
        const Stream full{true, false, 30};
        const Stream empty{false, false, 100};
        const TDiskIOPlanner::Round round = planner.plan({empty, full, empty}, false, 0, false, 2, 1);
        check(round.streams == std::vector<int>({1}) && round.idleSteps == 0, "writers with data only");
    }

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all checks passed\n");
    return 0;
}
