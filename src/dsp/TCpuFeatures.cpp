// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TCpuFeatures.h"

#if defined(__x86_64__) || defined(_M_X64)
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

// XCR0: which register states the operating system saves (XGETBV, only
// when CPUID says OSXSAVE)
#if !defined(_MSC_VER)
__attribute__((target("xsave")))
#endif
static unsigned long long xcr0()
{
    return _xgetbv(0);
}
#endif

TCpuFeatures TCpuFeatures::detect()
{
    TCpuFeatures cpu;
#if defined(TRAVERSO_CPU_X86)
    // Leaf 1: the feature bits in edx, FXSAVE in bit 24, SSE in bit 25; in
    // ecx OSXSAVE in bit 27, AVX in bit 28. Leaf 7: AVX2 in ebx bit 5
    unsigned int ecx = 0, edx = 0, leaf7ebx = 0;
#if defined(_MSC_VER)
    int registers[4] = {};
    __cpuid(registers, 0);
    const int maxLeaf = registers[0];
    __cpuid(registers, 1);
    ecx = static_cast<unsigned int>(registers[2]);
    edx = static_cast<unsigned int>(registers[3]);
    if (maxLeaf >= 7) {
        __cpuidex(registers, 7, 0);
        leaf7ebx = static_cast<unsigned int>(registers[1]);
    }
#else
    unsigned int eax = 0, ebx = 0;
    if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
        return cpu;
    }
    unsigned int leaf7eax = 0, leaf7ecx = 0, leaf7edx = 0;
    if (!__get_cpuid_count(7, 0, &leaf7eax, &leaf7ebx, &leaf7ecx, &leaf7edx)) {
        leaf7ebx = 0;
    }
#endif
    cpu.sse = (edx & (1u << 25)) != 0;
    // MXCSR, and FTZ in it, came with SSE
    cpu.flushToZero = cpu.sse;
    // DAZ (bit 6) only where the CPU accepts it: an early SSE2 CPU does not
    const bool fxsave = (edx & (1u << 24)) != 0;
    cpu.denormalsAreZero = cpu.sse && fxsave && (mxcsr_mask() & 0x0040) != 0;
    // AVX2 only when the operating system saves the XMM and YMM registers
    // (XCR0 bits 1 and 2): otherwise a context switch corrupts them
    const bool osSavesYmm = (ecx & (1u << 27)) != 0 && (ecx & (1u << 28)) != 0 && (xcr0() & 0x6) == 0x6;
    cpu.avx2 = osSavesYmm && (leaf7ebx & (1u << 5)) != 0;
#endif
    return cpu;
}
