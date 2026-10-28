// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>

// Sample frame count.
using nframes_t = std::uint32_t;

// One audio sample.
using audio_sample_t = float;

// A clock reading or interval, 64 bits on every platform. Holds nanoseconds,
// except the input event dispatcher and the driver setup messages, which hold
// milliseconds (T13 gives each clock one unit).
using trav_time_t = std::int64_t;
