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
class TAudioEncoder;
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
 * instance: open(), write() as the audio comes, close(). Not for the
 * audio thread: it allocates and does file IO.
 *
 * The only user is TBufferedAudioStreamWriter (recordings, imports, merges,
 * exports); the codec writers know nothing of rates or planar buffers.
 */
class TAudioWriter
{
public:
    // A new file at fileName in the settings' format, for audio at
    // inputSampleRate (the file's rate is the settings'; another input rate
    // is resampled): the one way to write an audio file. nullptr when the
    // file cannot be made (logged): no encoder for the format in this build,
    // or no file there.
    static std::unique_ptr<TAudioWriter> open(const TAudioEncodeSettings& settings, const QString& fileName, uint inputSampleRate);

    // Closes the file if close() did not
    ~TAudioWriter();

    // frameCount frames of the channel buffers of fileIOBuffer, at the input
    // rate; the frames taken (what is written may be fewer or more, at the
    // file's rate)
    nframes_t write(TFileIOBuffer& fileIOBuffer, nframes_t frameCount);
    // The file complete (the resampler's rest, the encoder's end, the
    // header); false when that failed. Once: the writer writes no more after.
    bool close();

    uint input_rate() const { return m_inputSampleRate; }
    uint file_rate() const { return m_settings.sampleRate; }
    uint channel_count() const { return m_settings.channelCount; }

private:
    TAudioWriter(const TAudioEncodeSettings& settings, std::unique_ptr<TAudioEncoder> encoder, uint inputSampleRate);

    // At close: what the resampler still holds into the file; false when it
    // could not all be written
    bool flush_resampler();
    // The channels of buffer, frames long, into its interleaved part
    void interleave(TFileIOBuffer& buffer, nframes_t frames);
    // frames frames of buffer's interleaved part into the file, in the
    // file's format (packed in buffer's byte part when it is not float)
    nframes_t write_interleaved(TFileIOBuffer& buffer, nframes_t frames);

    std::unique_ptr<TAudioEncoder>                  m_encoder;
    TAudioEncodeSettings                            m_settings;

    // The rate of the audio handed to write(); the file's rate and its
    // channels are the settings'
    uint                                            m_inputSampleRate;
    bool                                            m_closed{false};

    // All channels at once: every channel gives the same frames
    std::unique_ptr<TAudioResampler>                m_resampler;
    GDither                                         m_dither;
    // What the resampler gives, interleaved and packed there, like the
    // decode buffer of TAudioReader; the caller's buffer is only
    // read then
    TFileIOBuffer                                   m_resampleEncodeBuffer;
};
