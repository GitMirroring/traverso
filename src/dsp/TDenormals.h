// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "TRealTime.h"

// Denormals (the tails of fades, filters and reverbs) make a cycle many times
// slower. On x86 the audio threads flush them to zero: flush-to-zero (FTZ)
// and, where the CPU has it, denormals-are-zero (DAZ), bits of the
// per-thread MXCSR. Which bits is decided once before main() (cpuid, and
// none under TRAVERSO_RUNNING_UNDER_VALGRIND); enforce() sets them on the
// thread it runs on. TAudioDevice::run_cycle() calls it every cycle: a driver
// that makes a new thread, or a plugin that changes MXCSR, is caught at the
// next cycle. Elsewhere it does nothing (ARM flushes through FPCR.FZ: later).
class TDenormals
{
public:
    // Sets the bits on this thread when one is missing: reads MXCSR, and
    // writes it only then
    static void enforce() T_REALTIME_SAFE;

    // "FTZ and DAZ", "FTZ", "none (valgrind)", "none (not x86)"
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
