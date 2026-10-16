// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of TFrameRateFilter: at a 60 Hz frame clock a consumer gets
// about the rate it asked for, never more than one update per tick, and
// jittery tick times do not change that.

#include "TFrameRateFilter.h"

#include <cstdio>

static int failures = 0;

static void check(bool ok, const char* what)
{
    std::printf("%s: %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) {
        ++failures;
    }
}

static const qint64 frame = 1000000000LL / 60;

// Updates during ten seconds of ticks; jitter alternates the tick times
static int updates_in_ten_seconds(int maxHz, qint64 jitter)
{
    TFrameRateFilter filter(maxHz);
    int updates = 0;
    for (int i = 0; i < 600; ++i) {
        const qint64 now = i * frame + ((i % 2) ? jitter : -jitter);
        if (filter.due(now, frame)) {
            ++updates;
        }
    }
    return updates;
}

int main()
{
    int n = updates_in_ten_seconds(8, 0);
    check(n >= 79 && n <= 81, "8 Hz at 60 frames per second: about 80 updates in ten seconds");

    n = updates_in_ten_seconds(25, 0);
    check(n >= 248 && n <= 252, "25 Hz: about 250 updates, alternating two and three frames");

    check(updates_in_ten_seconds(60, 0) == 600, "60 Hz: every tick");
    check(updates_in_ten_seconds(1000, 0) == 600, "1000 Hz: every tick, never more");

    n = updates_in_ten_seconds(8, 3000000);
    check(n >= 78 && n <= 82, "8 Hz with 3 ms tick jitter: still about 80 updates");

    {
        TFrameRateFilter filter(8);
        check(filter.due(0, frame), "the first tick is due");
        check(!filter.due(frame, frame), "the next tick is not");
        filter.reset();
        check(filter.due(2 * frame, frame), "after a reset the next tick is due");
    }

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all frame rate filter tests passed\n");
    return 0;
}
