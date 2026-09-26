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

class TExportSpecification;
class AbstractAudioWriter;
class TAudioResampler;
class TFileIOBuffer;

class TResampleAudioWriter
{
public:
    TResampleAudioWriter(TExportSpecification* spec);
    ~TResampleAudioWriter();

    bool open(const QString& filename, uint inputSampleRate);
    nframes_t write_planar(TFileIOBuffer& fileIOBuffer, nframes_t frameCount);
    bool close();

private:
    std::unique_ptr<AbstractAudioWriter>            m_codecWriter;
    TExportSpecification*                           m_spec;

    uint                                            m_inputSampleRate;
    uint                                            m_outputSampleRate;
    uint                                            m_channelCount;

    std::vector<std::unique_ptr<TAudioResampler>>   m_resamplers;
    GDither                                         m_dither;
};
