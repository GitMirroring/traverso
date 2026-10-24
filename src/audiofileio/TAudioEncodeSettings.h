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
    // The resampler when the input rate is not sampleRate: a backend and one
    // of its qualities
    TAudioResampler::BackendType resampler{TAudioResampler::BackendType::LIBSOXR};
    int                     resampleQuality{TAudioResampler::default_quality(TAudioResampler::BackendType::LIBSOXR)};
    GDitherType             ditherType{};

    // WavPack: how hard it compresses, and whether it leaves out the
    // correction data of floating point samples (WVX, lossy below 25 bits)
    enum class WavPackCompression { Fast, High, VeryHigh };
    WavPackCompression      wavpackCompression{WavPackCompression::High};
    bool                    wavpackSkipWvx{false};

    // Of the sample format: the bits of a sample in the file, and the size
    // gdither writes it in (a 24 bit sample in a 32 bit word)
    int bit_depth() const { return bit_depth(dataFormat); }
    GDitherSize dither_size() const { return dither_size(dataFormat); }

    static int bit_depth(TraversoDAW::DataFormat format)
    {
        switch (format) {
        case TraversoDAW::DataFormat::PCM_S8: return 8;
        case TraversoDAW::DataFormat::PCM_16: return 16;
        case TraversoDAW::DataFormat::PCM_24: return 24;
        case TraversoDAW::DataFormat::PCM_32:
        case TraversoDAW::DataFormat::FLOAT: return 32;
        }
        return 32;
    }

    static GDitherSize dither_size(TraversoDAW::DataFormat format)
    {
        switch (format) {
        case TraversoDAW::DataFormat::PCM_S8: return GDither8bit;
        case TraversoDAW::DataFormat::PCM_16: return GDither16bit;
        case TraversoDAW::DataFormat::PCM_24: return GDither32bit;
        default: return GDitherFloat;
        }
    }
};
