// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "defines.h"
#include "gdither_types.h"
#include "TAudioResampler.h"

/**
 * @brief What an audio writer needs to know to encode a file: the writer and
 *        file format, the sample format, channels, sample rate, resampler and
 *        dithering.
 *
 * TExportSpecification in core provides it for an export or a recording, so
 * the writers do not depend on core.
 */
struct TAudioEncodeSettings
{
    TraversoDAW::WriterType writerType{};
    TraversoDAW::FileFormat fileFormat{};
    TraversoDAW::DataFormat dataFormat{};
    uint                    channelCount{0};
    uint                    sampleRate{0};
    uint                    blockSize{0};
    uint                    sampleBytes{0};
    int                     bitDepth{0};
    // The resampler when the input rate is not sampleRate: a backend and one
    // of its qualities
    TAudioResampler::BackendType resampler{TAudioResampler::BackendType::LIBSOXR};
    int                     resampleQuality{TAudioResampler::default_quality(TAudioResampler::BackendType::LIBSOXR)};
    GDitherType             ditherType{};
    GDitherSize             ditherSize{};
};
