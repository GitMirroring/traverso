// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of TAudioBuffer: no channels before check_capacity(), and a
// buffer that served a stereo source (as the decode buffer of a disk thread
// does) has room for a source with more channels, whether the new read is
// smaller or larger than the ones before. That a file read through it keeps
// its channels apart is the reader's: tests/audiofileio/test_reader_buffer_reuse.cpp.

#include "TAudioBuffer.h"

#include <cstdio>
#include <type_traits>

static int failures = 0;

static void check(bool ok, const char* what)
{
    std::printf("%s: %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) {
        ++failures;
    }
}

// A channel buffer owns a raw block of memory: a copy or a move would free it
// twice; containers hold it by unique_ptr
static_assert(!std::is_copy_constructible_v<TChannelBuffer> && !std::is_copy_assignable_v<TChannelBuffer>);
static_assert(!std::is_move_constructible_v<TChannelBuffer> && !std::is_move_assignable_v<TChannelBuffer>);

int main()
{
    // No channels before the first check_capacity(), then the ones asked
    {
        TAudioBuffer buffer;
        check(buffer.channel_count() == 0, "a new buffer has no channels");
        buffer.check_capacity(100, 3);
        check(buffer.channel_count() == 3 && buffer.get_channel_buffer(2).get_size() >= 100, "check_capacity() makes the channels at the size asked");
    }

    // More channels with a read smaller than the ones before
    {
        TAudioBuffer buffer;
        buffer.check_capacity(4096, 2);
        buffer.check_capacity(1024, 4);
        bool roomy = buffer.channel_count() == 4;
        for (uint channel = 0; channel < 4; ++channel) {
            roomy = roomy && buffer.get_channel_buffer(channel).get_size() >= 1024;
        }
        check(roomy, "after stereo, a smaller read of 4 channels has room for each channel");
    }

    // More channels with a read larger than the ones before
    {
        TAudioBuffer buffer;
        buffer.check_capacity(1024, 2);
        buffer.check_capacity(30720, 4);
        bool roomy = buffer.channel_count() == 4;
        for (uint channel = 0; channel < 4; ++channel) {
            roomy = roomy && buffer.get_channel_buffer(channel).get_size() >= 30720;
        }
        check(roomy, "after stereo, a larger read of 4 channels has room for each channel");
    }

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all TAudioBuffer tests passed\n");
    return 0;
}
