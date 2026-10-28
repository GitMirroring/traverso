// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TDenormals.h"
#include "TCpuFeatures.h"

#include <cstdlib>

#if defined(__x86_64__) || defined(_M_X64) || (defined(__i386__) && defined(__SSE__)) || (defined(_M_IX86_FP) && _M_IX86_FP >= 1)
#define TRAVERSO_DENORMALS_MXCSR 1
#include <xmmintrin.h>
#endif

// The MXCSR bits
static constexpr unsigned FlushToZero      = 0x8000;  // bit 15
static constexpr unsigned DenormalsAreZero = 0x0040;  // bit 6

constinit unsigned TDenormals::s_mask = 0;
constinit const char* TDenormals::s_modeName = "none (not x86)";

// Before main(), so before any audio thread, and off it: cpuid and getenv
// never run in a cycle. No log here (TLog may not exist yet): Traverso logs
// mode_name().
bool TDenormals::decide()
{
#if defined(TRAVERSO_DENORMALS_MXCSR)
    if (std::getenv("TRAVERSO_RUNNING_UNDER_VALGRIND")) {
        s_modeName = "none (valgrind)";
        return true;
    }
    const TCpuFeatures cpu;
    if (cpu.has_flush_to_zero()) {
        s_mask |= FlushToZero;
    }
    if (cpu.has_denormals_are_zero()) {
        s_mask |= DenormalsAreZero;
    }
    s_modeName = s_mask == (FlushToZero | DenormalsAreZero) ? "FTZ and DAZ" : s_mask == FlushToZero ? "FTZ" : "none";
#endif
    return true;
}

const bool TDenormals::s_decided = decide();

void TDenormals::enforce() T_REALTIME_SAFE
{
#if defined(TRAVERSO_DENORMALS_MXCSR)
    const unsigned csr = _mm_getcsr();
    if ((csr & s_mask) != s_mask) {
        _mm_setcsr(csr | s_mask);
    }
#endif
}

bool TDenormals::active_on_this_thread() T_REALTIME_SAFE
{
#if defined(TRAVERSO_DENORMALS_MXCSR)
    return (_mm_getcsr() & s_mask) == s_mask;
#else
    return true;
#endif
}
