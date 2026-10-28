// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TCpuFeatures.h"

#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
#define TRAVERSO_CPU_X86 1
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#endif

TCpuFeatures TCpuFeatures::detect()
{
    TCpuFeatures cpu;
#if defined(TRAVERSO_CPU_X86)
    // Leaf 1: the feature bits, SSE in bit 25 of edx
    unsigned int edx = 0;
#if defined(_MSC_VER)
    int registers[4] = {};
    __cpuid(registers, 1);
    edx = static_cast<unsigned int>(registers[3]);
#else
    unsigned int eax = 0, ebx = 0, ecx = 0;
    if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
        return cpu;
    }
#endif
    cpu.sse = (edx & (1u << 25)) != 0;
    // MXCSR, and FTZ in it, came with SSE
    cpu.flushToZero = cpu.sse;
#if defined(__x86_64__) || defined(_M_X64)
    // Every 64-bit x86 CPU has DAZ. On 32-bit x86 an early SSE2 CPU may not,
    // and setting the bit there faults: 32-bit keeps FTZ only.
    cpu.denormalsAreZero = cpu.sse;
#endif
#endif
    return cpu;
}
