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

    // Where frames of two rates coincide
    {
        double fraction = 1.0;
        // 44.1 and 48 kHz meet every 147 and 160 frames (94080 universal):
        // 157696 frames at 48 kHz lie 96 past such a point
        const TTimeRef position(nframes_t(157696), 48000);
        const TTimeRef common = TTimeRef::common_frame_at_or_before(position, 44100, 48000);
        check(common.universal_frame() % 94080 == 0 && common <= position && position - common < TTimeRef(qint64(94080)),
              "44.1 and 48 kHz: the last common frame before the position");
        check(TTimeRef::frame_at_or_before(position - common, 48000) == 96, "44.1 and 48 kHz: 96 frames past it");
        TTimeRef::frame_at_or_before(common, 44100, &fraction);
        check(fraction == 0.0, "44.1 and 48 kHz: a whole frame of 44.1 kHz");
        TTimeRef::frame_at_or_before(common, 48000, &fraction);
        check(fraction == 0.0, "44.1 and 48 kHz: a whole frame of 48 kHz");

        // Equal rates: a frame is a common frame
        check(TTimeRef::common_frame_at_or_before(position, 48000, 48000) == position, "equal rates: the position itself");
        // Every frame of 48 kHz is one of 96 kHz too
        check(TTimeRef::common_frame_at_or_before(position, 96000, 48000) == position, "96 and 48 kHz: every 48 kHz frame");
        // Between two frames: the frame before
        const TTimeRef between(position.universal_frame() + 100);
        check(TTimeRef::common_frame_at_or_before(between, 48000, 48000) == position, "equal rates between frames: the frame before");

        // 384 kHz is no divisor of the universal rate, but with 48 kHz the
        // common frames are (every 8 frames of 384 kHz)
        const TTimeRef at384 = TTimeRef::common_frame_at_or_before(position, 384000, 48000);
        TTimeRef::frame_at_or_before(at384, 384000, &fraction);
        check(at384 == position && fraction == 0.0, "384 and 48 kHz: the 48 kHz frame, a whole 384 kHz frame");
        // 384 and 768 kHz meet at no whole universal frame: the time itself
        check(TTimeRef::common_frame_at_or_before(between, 384000, 768000) == between, "384 and 768 kHz: the time itself");

        // Before zero: at or before, not towards zero
        const TTimeRef negative(qint64(-1000));
        const TTimeRef beforeZero = TTimeRef::common_frame_at_or_before(negative, 44100, 48000);
        check(beforeZero == TTimeRef(qint64(-94080)), "before zero: the common frame before, not after");
    }

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all TTimeRef tests passed\n");
    return 0;
}
