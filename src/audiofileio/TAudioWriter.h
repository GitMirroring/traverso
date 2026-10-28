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
#include <optional>

#include "TAudioEncodeSettings.h"
#include "TAudioFileError.h"
#include "TAudioBuffer.h"
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
 * audio goes through. Of the caller's buffer it only reads the channels; the
 * buffer has every channel of the file (the caller sizes it). It interleaves
 * and packs in scratch of its own, one per thread (used within one call
 * only). One file per instance: open(), write() as the audio comes,
 * close(). Not for the audio thread: it allocates and does file IO. One
 * thread at a time: the owner serializes write() and close(). A writer
 * destroyed without close() closes in its destructor (release builds), on
 * the thread that destroys it.
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
    // holds some of them for a later write or close(); 0 with the error when
    // the file could not take them. After a failed write the writer stops:
    // every later write gives that first error without touching the file;
    // close() still completes what was written, and can fail again with an
    // error of its own (the resampler's flush goes to the file). The file's own frames, at its
    // rate, are another count. A caller that only needs the count writes
    // .frames.
    TAudioFileResult write(const TAudioBuffer& fileIOBuffer, nframes_t frameCount);
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

    // write() without the stop after a failure
    TAudioFileResult write_frames(const TAudioBuffer& fileIOBuffer, nframes_t frameCount);
    // At close: what the resampler still holds into the file; the error when
    // it could not all be written
    TAudioFileStatus flush_resampler();
    // The channels of buffer, frames long, interleaved in the writer's
    // scratch (per thread); a pointer to it
    float* interleave(const TAudioBuffer& buffer, nframes_t frames);
    // frames interleaved frames into the file, in the file's format (packed
    // in the writer's scratch when it is not float)
    TAudioFileResult write_interleaved(float* interleaved, nframes_t frames);

    std::unique_ptr<TAudioEncoder>                  m_encoder;
    TAudioEncodeSettings                            m_settings;

    // The rate of the audio handed to write(); the file's rate and its
    // channels are the settings'
    uint                                            m_inputSampleRate;
    bool                                            m_closed{false};
    // The first failed write's error: the writer writes no more after it
    std::optional<TAudioFileError>                  m_failed;

    // All channels at once: every channel gives the same frames
    std::unique_ptr<TAudioResampler>                m_resampler;
    GDither                                         m_dither;
    // What the resampler gives, per channel (the input frames become
    // another count of output frames)
    TAudioBuffer                                   m_resampleEncodeBuffer;
};
