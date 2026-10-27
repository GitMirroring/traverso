// SPDX-FileCopyrightText: 2007 Ben Levitt
// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "defines.h"

#include "TAudioEncodeSettings.h"
#include "TAudioFileError.h"

#include <QString>

#include <memory>

/**
 * @brief Writes one file in one format: the counterpart of TAudioDecoder.
 * One class per library (SFAudioWriter: libsndfile, WPAudioWriter: WavPack,
 * FaacAudioWriter: AAC), made by create_audio_encoder() from the settings.
 *
 * An encoder does all of its work itself: it opens the file, encodes what it
 * is given and completes the file when it closes (the header, what the codec
 * still holds). It knows nothing of rates or planar buffers:
 * TAudioWriter resamples, interleaves and converts the sample format
 * before. Not for the audio thread.
 */
class TAudioEncoder
{
public:
    virtual ~TAudioEncoder() = default;

    // A new file at fileName in the settings' format; an encoder that has a
    // file open closes it first. Why not when the file cannot be made
    // (CannotOpen).
    [[nodiscard]] virtual TAudioFileStatus open(const QString& fileName) = 0;
    // frameCount interleaved frames of buffer, in the settings' sample format
    // as gdither writes it (float; 8 and 16 bit; 24 and 32 bit in 32 bit
    // words): the frames written, and the error when writing failed
    // (WriteFailed; also without a file open)
    virtual TAudioFileResult write(void* buffer, nframes_t frameCount) = 0;
    // The file complete, or why not (WriteFailed). Without a file open:
    // nothing to do, no error.
    [[nodiscard]] virtual TAudioFileStatus close() = 0;

protected:
    explicit TAudioEncoder(const TAudioEncodeSettings& settings) : m_settings(settings) {}

    const TAudioEncodeSettings m_settings;
};

// The encoder for the settings' writer type; nullptr when this build has
// none for it (AAC without FAAC)
std::unique_ptr<TAudioEncoder> create_audio_encoder(const TAudioEncodeSettings& settings);
