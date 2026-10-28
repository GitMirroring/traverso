// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QtGlobal>

// The resampler a reader or a writer uses, as its owner chose it: a backend
// and one of its qualities, which belong together (a soxr quality means
// nothing to libsamplerate). dsp makes the resampler from it
// (TAudioResampler); audiofileio, the IO scheduler and core only hand it on,
// so it lives here, below all of them. The quality is a plain number: it is
// checked against TAudioResampler::qualities() where it enters from the
// configuration (configured_resampler(), the export dialog), and trusted
// behind that. No default: every maker sets it.
struct TResamplerChoice
{
    enum class Backend : qint8 { LibSampleRate, LibSoxr };

    Backend backend;
    // One of TAudioResampler::qualities(backend), in the backend's numbers
    qint16 quality;

    bool operator==(const TResamplerChoice&) const = default;
};
