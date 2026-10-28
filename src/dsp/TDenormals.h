// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "TRealTime.h"

// x86-64: the MXCSR exists (the one place this is decided; the test uses it
// too). 32-bit x86 is not supported (CMakeLists.txt)
#if defined(__x86_64__) || defined(_M_X64)
#define TRAVERSO_DENORMALS_MXCSR 1
#endif

// Denormals (the tails of fades, filters and reverbs) make a cycle many times
// slower. On x86 the audio threads flush them to zero: flush-to-zero (FTZ)
// and, where the CPU has it, denormals-are-zero (DAZ), bits of the
// per-thread MXCSR. Which bits is decided once before main() (cpuid, and
// none under TRAVERSO_RUNNING_UNDER_VALGRIND); enforce() sets them on the
// thread it runs on. Two calls cover all threads:
// - Traverso's constructor, on the main thread before any other thread: the
//   threads Traverso starts (DiskIO, the pools, the converters) inherit it;
// - TAudioDevice::run_cycle(), every cycle: threads Traverso did not start
//   (JACK, PipeWire, CoreAudio), a driver that makes a new thread, a plugin
//   that changes MXCSR, all caught at the next cycle.
// Without SSE it does nothing (ARM flushes through FPCR.FZ: later).
class TDenormals
{
public:
    // Sets the bits on this thread when one is missing: reads MXCSR, and
    // writes it only then
    static void enforce() T_REALTIME_SAFE;

    // "FTZ and DAZ", "FTZ", "none (valgrind)", "none (no SSE)"
    static const char* mode_name() { return s_modeName; }

    // The bits enforce() sets, and whether this thread has them all (tests)
    static unsigned mask() { return s_mask; }
    static bool active_on_this_thread() T_REALTIME_SAFE;

private:
    static bool decide();
    static unsigned s_mask;
    static const char* s_modeName;
    static const bool s_decided;
};
