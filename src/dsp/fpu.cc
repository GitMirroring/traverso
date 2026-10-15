/*
Copyright (C) 2007 - 2026 Remon Sijrier

Copyright (C) 2000-2007 Paul Davis

This file is part of Traverso

Traverso is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA.

*/

#include <cassert>
#include <cstdint>

#if (defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86))
#define TRAVERSO_ARCH_X86 1
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#endif

#include "fpu.h"

FPU::FPU ()
{
    _flags = Flags(0);

#if !defined(TRAVERSO_ARCH_X86)
    // Non-x86 platforms (e.g., ARM64) do not use x86-specific FPU/MXCSR flags
    return;
#else
    uint32_t edx = 0;
    uint32_t ecx = 0;

#if defined(_MSC_VER)
    int cpuInfo[4];
    __cpuid(cpuInfo, 1);
    ecx = static_cast<uint32_t>(cpuInfo[2]);
    edx = static_cast<uint32_t>(cpuInfo[3]);
#else
    unsigned int eax = 0, ebx = 0;
    if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
        // Registers ecx and edx are now fully populated by the compiler intrinsic
    }
#endif

    // Bit 25 of EDX indicates baseline SSE support
    if (edx & (1 << 25)) {
        _flags = Flags(_flags | (HasSSE | HasFlushToZero));
    }

    // Bit 26 of EDX indicates baseline SSE2 support
    if (edx & (1 << 26)) {
        _flags = Flags(_flags | HasSSE2);
    }

    // Bit 24 of EDX indicates FXSAVE/FXRSTOR support
    if (edx & (1 << 24)) {
        // Modern distribution-safe check for Denormals-Are-Zero (DAZ) support.
        // Instead of raw inline assembly fxsave, DAZ availability can be reliably inferred
        // on all modern processors supporting modern SSE2 baselines.
        if (_flags & HasSSE2) {
            _flags = Flags(_flags | HasDenormalsAreZero);
        }
    }
#endif
}

FPU::~FPU ()
{
}
