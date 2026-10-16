// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of TTimeRef conversions that have no rounding slack:
// nanoseconds to universal frames.

#include "TTimeRef.h"

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
    const qint64 rate = TTimeRef::UNIVERSAL_SAMPLE_RATE;

    check(TTimeRef::from_nanoseconds(0).universal_frame() == 0, "zero");
    check(TTimeRef::from_nanoseconds(1000000000).universal_frame() == rate, "one second");
    check(TTimeRef::from_nanoseconds(1000000).universal_frame() == rate / 1000, "one millisecond");
    check(TTimeRef::from_nanoseconds(1).universal_frame() == 0, "one nanosecond truncates to zero");
    check(TTimeRef::from_nanoseconds(-1000000000).universal_frame() == -rate, "minus one second");
    check(TTimeRef::from_nanoseconds(-1000000).universal_frame() == -rate / 1000, "minus one millisecond");

    // One hour, one second and half a second: whole seconds and rest together
    const qint64 nanoseconds = (3601LL * 1000000000) + 500000000;
    check(TTimeRef::from_nanoseconds(nanoseconds).universal_frame() == 3601 * rate + rate / 2, "hours with a rest");

    // Elapsed times add up like the frames they stand for
    const TTimeRef a = TTimeRef::from_nanoseconds(300000000);
    const TTimeRef b = TTimeRef::from_nanoseconds(700000000);
    check((a + b).universal_frame() == rate, "parts add up to a second");

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all TTimeRef tests passed\n");
    return 0;
}
