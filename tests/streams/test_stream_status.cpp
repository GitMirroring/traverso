// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of TBufferedAudioStreamStatus: when a stream needs work from
// the scheduler

#include "TBufferedAudioStreamStatus.h"

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


    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all checks passed\n");
    return 0;
}
