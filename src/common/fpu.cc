/*
Copyright (C) 2007 Remon Sijrier

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
#include <cstring>
#include <stdlib.h>
#include <stdint.h>

#if (defined __x86_64__) || (defined __i386__)
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#endif

#include <fpu.h>

FPU::FPU ()
{
    uint32_t cpuflags = 0;

    _flags = Flags (0);

#if !( (defined __x86_64__) || (defined __i386__) ) // !ARCH_X86
    (void)cpuflags;
    return;
#else

#if defined(_MSC_VER)
    int cpuInfo[4];
    __cpuid(cpuInfo, 1);
    cpuflags = static_cast<uint32_t>(cpuInfo[3]);
#else
    unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
    if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
        cpuflags = edx;
    }
#endif

    if (cpuflags & (1<<25)) {
        _flags = Flags (_flags | (HasSSE|HasFlushToZero));
    }

    if (cpuflags & (1<<26)) {
        _flags = Flags (_flags | HasSSE2);
    }

    if (cpuflags & (1 << 24)) {

        /* DAZ wasn't available in the first version of SSE. Since
           setting a reserved bit in MXCSR causes a general protection
           fault, we need to be able to check the availability of this
           feature without causing problems. To do this, one needs to
           set up a 512-byte area of memory to save the SSE state to,
           using fxsave, and then one needs to inspect bytes 28 through
           31 for the MXCSR_MASK value. If bit 6 is set, DAZ is
           supported, otherwise, it isn't.
        */

        alignas(16) char fxbuf[512];
        memset (fxbuf, 0, sizeof(fxbuf));

        asm volatile (
            "fxsave (%0)"
            :
            : "r" (fxbuf)
            : "memory"
            );

        uint32_t mxcsr_mask = *((uint32_t*) &(fxbuf[28]));

        /* if the mask is zero, set its default value (from intel specs) */

        if (mxcsr_mask == 0) {
            mxcsr_mask = 0xffbf;
        }

        if (mxcsr_mask & (1<<6)) {
            _flags = Flags (_flags | HasDenormalsAreZero);
        }
    }
#endif
}

FPU::~FPU ()
{
}
