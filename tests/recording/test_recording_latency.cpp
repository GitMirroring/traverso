// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of the recording latency compensation (TRecordingLatency)

#include "TRecordingLatency.h"

#include <cstdio>

static int failures = 0;

static void check(bool ok, const char* what)
{
    if (!ok) {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}

int main()
{
    // Capture plus playback: ALSA with 256 frames and 3 periods queues 2 for
    // playback and captures 1
    check(TRecordingLatency::compensation_frames(256, 512, 0.0, 48000, true, true) == 768, "capture plus playback");
    // The user's extra latency in milliseconds, also below zero
    check(TRecordingLatency::compensation_frames(256, 512, 1.0, 48000, true, true) == 768 + 48, "plus 1 ms");
    check(TRecordingLatency::compensation_frames(256, 512, -2.0, 48000, true, true) == 768 - 96, "minus 2 ms");
    check(TRecordingLatency::compensation_frames(256, 512, -100.0, 48000, true, true) == 0, "never below 0");
    check(TRecordingLatency::compensation_frames(64, 64, 0.5, 44100, true, true) == 128 + 22, "0.5 ms at 44.1 kHz rounds");
    // Not compensated: switched off, a software input, no rate
    check(TRecordingLatency::compensation_frames(256, 512, 1.0, 48000, true, false) == 0, "switched off");
    check(TRecordingLatency::compensation_frames(256, 512, 1.0, 48000, false, true) == 0, "software input");
    check(TRecordingLatency::compensation_frames(256, 512, 1.0, 0, true, true) == 0, "no rate");

    // The take's length grows only past the compensation
    check(TRecordingLatency::visible_frames(0, 768) == 0, "nothing written");
    check(TRecordingLatency::visible_frames(700, 768) == 0, "still inside the compensation");
    check(TRecordingLatency::visible_frames(768, 768) == 0, "exactly the compensation");
    check(TRecordingLatency::visible_frames(1024, 768) == 256, "past it");
    check(TRecordingLatency::visible_frames(1024, 0) == 1024, "no compensation");

    // The writer drops the compensation over the first buffers: whole ones,
    // then part of one, then nothing
    qint64 remaining = 700;
    check(TRecordingLatency::frames_to_drop(remaining, 256) == 256 && remaining == 444, "a whole buffer");
    check(TRecordingLatency::frames_to_drop(remaining, 256) == 256 && remaining == 188, "another whole buffer");
    check(TRecordingLatency::frames_to_drop(remaining, 256) == 188 && remaining == 0, "part of a buffer");
    check(TRecordingLatency::frames_to_drop(remaining, 256) == 0 && remaining == 0, "nothing more");
    remaining = 512;
    check(TRecordingLatency::frames_to_drop(remaining, 256) == 256 && TRecordingLatency::frames_to_drop(remaining, 256) == 256
          && remaining == 0 && TRecordingLatency::frames_to_drop(remaining, 256) == 0, "exactly two buffers");
    // What the clip shows and what the file holds stay the same length
    qint64 captured = 0;
    qint64 written = 0;
    remaining = 700;
    for (int cycle = 0; cycle < 10; ++cycle) {
        captured += 256;
        written += 256 - TRecordingLatency::frames_to_drop(remaining, 256);
        check(TRecordingLatency::visible_frames(captured, 700) == written, "clip length equals the file");
    }

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all recording latency tests passed\n");
    return 0;
}
