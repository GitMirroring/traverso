/*
Copyright (C) 2007-2026 Ben Levitt, Remon Sijrier

This file is part of Traverso

Traverso is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "defines.h"
#include "gdither_types.h"
#include <QString>
#include <memory>
#include <vector>

#include "TAudioEncodeSettings.h"
#include "TFileIOBuffer.h"
class AbstractAudioWriter;
class TAudioResampler;

class TResampleAudioWriter
{
public:
    TResampleAudioWriter(const TAudioEncodeSettings& settings);
    ~TResampleAudioWriter();

    bool open(const QString& filename, uint inputSampleRate);
    nframes_t write_planar(TFileIOBuffer& fileIOBuffer, nframes_t frameCount);
    bool close();

private:
    // At close: the resamplers' rest into the file
    void flush_resamplers();
    // The channels of buffer, frames long, into its interleaved part
    float* interleave(TFileIOBuffer& buffer, nframes_t frames);
    // targetFrames interleaved frames into the file, in the file's format
    nframes_t write_interleaved(TFileIOBuffer& fileIOBuffer, float* interleavedFloatBuffer, nframes_t targetFrames);
    // The frames to write once channel chan produced frames and the channels
    // before it the least so far: equal resamplers fed alike give alike, so
    // a difference is a bug (asserted, logged once); the least is written so
    // that no channel's unfilled tail reaches the file
    nframes_t least_frames(uint chan, nframes_t produced, nframes_t least);

    std::unique_ptr<AbstractAudioWriter>            m_codecWriter;
    TAudioEncodeSettings                            m_settings;

    uint                                            m_inputSampleRate;
    uint                                            m_outputSampleRate;
    uint                                            m_channelCount;

    std::vector<std::unique_ptr<TAudioResampler>>   m_resamplers;
    bool                                            m_frameCountMismatchLogged{false};
    GDither                                         m_dither;
    // What the resamplers give, interleaved and packed there, like the
    // decode buffer of TResampleAudioReader; the caller's buffer is only
    // read then
    TFileIOBuffer                                   m_resampleEncodeBuffer;
};
