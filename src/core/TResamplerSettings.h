// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "TIOStream.h"

// The resampler the settings choose, read from TConfig: in the GUI thread
// only. The streams get the value (TIOStream::Resampler) and read no
// settings themselves.

// Conversion/Resampler: soxr or libsamplerate, for on-the-fly reading and
// for exports
TResamplerChoice::Backend configured_resampler_backend();
// The resampler of on-the-fly reading: the backend, its quality
// (Conversion/RTResamplingQuality, the backend's own number; another number
// is the backend's default) and whether to resample at all
// (Conversion/DynamicResampling)
TIOStream::Resampler configured_resampler();
