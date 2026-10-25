// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TResamplerSettings.h"

#include "TConfig.h"

TAudioResampler::BackendType configured_resampler_backend()
{
    return TAudioResampler::backend_from_name(config().get_property("Conversion", "Resampler", "soxr").toString());
}

TIOStream::Resampler configured_resampler()
{
    const TAudioResampler::BackendType backend = configured_resampler_backend();
    int quality = config().get_property("Conversion", "RTResamplingQuality", -1).toInt();
    if (!TAudioResampler::qualities(backend).contains(quality)) {
        quality = TAudioResampler::default_realtime_quality(backend);
    }
    const bool resample = config().get_property("Conversion", "DynamicResampling", true).toBool();
    return {qint32(backend), qint16(quality), resample};
}
