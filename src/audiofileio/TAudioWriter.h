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
#include <expected>
#include <memory>
#include <vector>

#include "TAudioEncodeSettings.h"
#include "TAudioFileError.h"
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
 * audio goes through. Of the caller's buffer it reads the channels, after
 * making room for all of the file's (a channel the buffer lacks is
 * silence); without resampling it interleaves and packs in that buffer's
 * own interleaved and byte parts. One file per instance: open(), write() as
 * the audio comes, close(). Not for the audio thread: it allocates and does
 * file IO.
 *
 * The only user is TBufferedAudioStreamWriter (recordings, imports, merges,
 * exports); the codec writers know nothing of rates or planar buffers.
 */
class TAudioWriter
{
public:
    // A new file at fileName in the settings' format, for audio at
    // inputSampleRate (the file's rate is the settings'; another input rate
    // is resampled): the one way to write an audio file. Or why not
    // (CannotOpen): no encoder for the format in this build, no input rate
    // (0: no audio device yet), no resampler, or the file cannot be made.
    [[nodiscard]] static std::expected<std::unique_ptr<TAudioWriter>, TAudioFileError> open(const TAudioEncodeSettings& settings,
                                                                                          const QString& fileName, uint inputSampleRate);

    // close() is required: the destructor asserts it was called, and where
    // the assert is gone (a release build) it still closes and logs, so a
    // forgotten close() does not lose the end of a recording
    ~TAudioWriter();

    // frameCount frames of the channel buffers of fileIOBuffer, at the input
    // rate. The input frames taken: frameCount, also when the resampler
    // holds some of them for a later write or close(); fewer, with the
    // error, when the file could not take them (what the encoder took, or 0
    // when the input was resampled). After an error the caller stops
    // writing: the count is not what is in the file. The file's own frames, at its
    // rate, are another count. A caller that only needs the count writes
    // .frames.
    TAudioFileResult write(TFileIOBuffer& fileIOBuffer, nframes_t frameCount);
    // The file complete (the resampler's rest, the encoder's end, the
    // header), or why not (WriteFailed). The writer writes no more after; a
    // second close() does nothing and is no error.
    [[nodiscard]] TAudioFileStatus close();

    uint input_rate() const { return m_inputSampleRate; }
    uint file_rate() const { return m_settings.sampleRate; }
    uint channel_count() const { return m_settings.channelCount; }

private:
    TAudioWriter(const TAudioEncodeSettings& settings, std::unique_ptr<TAudioEncoder> encoder, std::unique_ptr<TAudioResampler> resampler,
                 uint inputSampleRate);

    // At close: what the resampler still holds into the file; the error when
    // it could not all be written
    TAudioFileStatus flush_resampler();
    // The channels of buffer, frames long, into its interleaved part
    void interleave(TFileIOBuffer& buffer, nframes_t frames);
    // frames frames of buffer's interleaved part into the file, in the
    // file's format (packed in buffer's byte part when it is not float)
    TAudioFileResult write_interleaved(TFileIOBuffer& buffer, nframes_t frames);

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
    // decode buffer of TAudioReader; of the caller's buffer only the
    // channels are read then
    TFileIOBuffer                                   m_resampleEncodeBuffer;
};
