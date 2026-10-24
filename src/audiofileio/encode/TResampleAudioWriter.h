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

/**
 * @brief Writes planar float audio to one file in the format the settings
 * describe: the codec (sndfile, WavPack, FLAC, Vorbis, MP3, AAC), the sample
 * format and the file's rate.
 *
 * Between the caller's buffer and the codec writer it does three things, in
 * this order: resamples when the input rate is not the file's (flushed at
 * close(), so the file keeps its last milliseconds), interleaves the
 * channels, and converts float to the file's sample format (8, 16 and 24
 * bit dithered through gdither, 32 bit scaled, float as it is). It owns the
 * codec writer, the resampler, the dither and the buffer the resampled
 * audio goes through; the caller's buffer is only read. One file per
 * instance: open(), write_planar() as the audio comes, close(). Not for the
 * audio thread: it allocates and does file IO.
 *
 * The only user is TBufferedAudioStreamWriter (recordings, imports, merges,
 * exports); the codec writers know nothing of rates or planar buffers.
 */
class TResampleAudioWriter
{
public:
    TResampleAudioWriter(const TAudioEncodeSettings& settings);
    ~TResampleAudioWriter();

    bool open(const QString& filename, uint inputSampleRate);
    nframes_t write_planar(TFileIOBuffer& fileIOBuffer, nframes_t frameCount);
    bool close();

private:
    // At close: what the resampler still holds into the file; false when it
    // could not all be written
    bool flush_resampler();
    // The channels of buffer, frames long, into its interleaved part
    void interleave(TFileIOBuffer& buffer, nframes_t frames);
    // frames frames of buffer's interleaved part into the file, in the
    // file's format (packed in buffer's byte part when it is not float)
    nframes_t write_interleaved(TFileIOBuffer& buffer, nframes_t frames);

    std::unique_ptr<AbstractAudioWriter>            m_codecWriter;
    TAudioEncodeSettings                            m_settings;

    // The rate of the audio handed to write_planar(); the file's rate and
    // its channels are the settings'
    uint                                            m_inputSampleRate;

    // All channels at once: every channel gives the same frames
    std::unique_ptr<TAudioResampler>                m_resampler;
    GDither                                         m_dither;
    // What the resampler gives, interleaved and packed there, like the
    // decode buffer of TResampleAudioReader; the caller's buffer is only
    // read then
    TFileIOBuffer                                   m_resampleEncodeBuffer;
};
