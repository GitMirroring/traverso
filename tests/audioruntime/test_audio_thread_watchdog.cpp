// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of TAudioThreadWatchdog with a simulated clock: an audio
// thread that keeps completing cycles is never stalled, however late or
// irregular the checks are; one that stops completing cycles is.

#include "TAudioThreadWatchdog.h"

#include <algorithm>
#include <cstdio>

static int failures = 0;

static void check(bool ok, const char* what)
{
    std::printf("%s: %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) {
        ++failures;
    }
}

int main()
{
    // A running audio thread: about 94 cycles per second (512 frames at
    // 48 kHz), checked every 500 ms
    {
        TAudioThreadWatchdog watchdog(2000);
        watchdog.restart(0, 0);
        bool stalled = false;
        for (qint64 now = 500; now <= 60000; now += 500) {
            stalled = stalled || watchdog.is_stalled(quint64(now * 94 / 1000), now);
        }
        check(!stalled, "a running audio thread is never stalled");
    }

    // The GUI was busy for 10 seconds (e.g. loading a project): the next
    // check is late, but the audio thread kept counting
    {
        TAudioThreadWatchdog watchdog(2000);
        watchdog.restart(0, 0);
        check(!watchdog.is_stalled(940, 10000), "a busy GUI is not a stall");
    }

    // The audio thread stops completing cycles after 5 seconds
    {
        TAudioThreadWatchdog watchdog(2000);
        watchdog.restart(0, 0);
        bool stalledTooEarly = false;
        bool stalled = false;
        for (qint64 now = 500; now <= 10000; now += 500) {
            const quint64 cycles = quint64(std::min<qint64>(now, 5000) * 94 / 1000);
            const bool result = watchdog.is_stalled(cycles, now);
            if (now < 7000 && result) stalledTooEarly = true;
            if (now >= 7000 && result) stalled = true;
        }
        check(!stalledTooEarly && stalled, "a thread that stops is stalled after 2 seconds");
    }

    // A restarted driver gets 2 seconds for its first cycle
    {
        TAudioThreadWatchdog watchdog(2000);
        watchdog.restart(500, 30000);
        check(!watchdog.is_stalled(500, 31500), "a restarted driver gets time for its first cycle");
        check(watchdog.is_stalled(500, 32000), "a restarted driver that never runs is stalled");
    }

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all audio thread watchdog tests passed\n");
    return 0;
}
