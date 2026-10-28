// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// What the CPU offers the audio path, as far as it asks: SSE for the buffer
// operations (TBufferOperations), flush-to-zero and denormals-are-zero for
// TDenormals. All false on a CPU that is not x86.
struct TCpuFeatures
{
    bool sse{false};
    // The MXCSR bit that flushes denormal results to zero (FTZ)
    bool flushToZero{false};
    // The MXCSR bit that reads denormal inputs as zero (DAZ)
    bool denormalsAreZero{false};

    // Asks the CPU (cpuid)
    static TCpuFeatures detect();
};
