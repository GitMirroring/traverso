// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TCpuFeatures.h"

#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
#define TRAVERSO_CPU_X86 1
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>
#include <immintrin.h>
#endif
#include <cstring>
#endif

#if defined(TRAVERSO_CPU_X86)
// The MXCSR bits this CPU accepts, as Intel documents it (SDM vol. 1,
// 11.6.6): FXSAVE writes them as MXCSR_MASK at byte 28; 0 there means the
// default mask 0xFFBF, which has no DAZ. Setting a bit outside it faults.
#if !defined(_MSC_VER)
__attribute__((target("fxsr")))
#endif
static unsigned int mxcsr_mask()
{
    alignas(16) unsigned char area[512] = {};
    _fxsave(area);
    unsigned int mask = 0;
    std::memcpy(&mask, area + 28, sizeof(mask));
    return mask != 0 ? mask : 0xFFBF;
}
#endif

TCpuFeatures TCpuFeatures::detect()
{
    TCpuFeatures cpu;
#if defined(TRAVERSO_CPU_X86)
    // Leaf 1: the feature bits in edx, FXSAVE in bit 24, SSE in bit 25
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
    // DAZ (bit 6) only where the CPU accepts it: an early SSE2 CPU does not
    const bool fxsave = (edx & (1u << 24)) != 0;
    cpu.denormalsAreZero = cpu.sse && fxsave && (mxcsr_mask() & 0x0040) != 0;
#endif
    return cpu;
}
