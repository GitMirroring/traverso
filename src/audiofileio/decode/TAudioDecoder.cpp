// SPDX-FileCopyrightText: 2007-2026 Ben Levitt, Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TAudioDecoder.h"

#include "TFileIOBuffer.h"

#include "SFAudioReader.h"
#include "WPAudioReader.h"
#include "MadAudioReader.h"

#if defined M4A_DECODE_SUPPORT
#include "FaadAudioReader.h"
#endif

#include "Debugger.h"

nframes_t TAudioDecoder::read_from(TFileIOBuffer& buffer, nframes_t first, nframes_t count)
{
    if (first != m_frame && !seek(first)) {
        // Nothing to read there: zeros, as read() gives past the end
        if (count > 0) {
            buffer.check_capacity(count, m_channels);
            buffer.silence(0, count, m_channels);
        }
        return 0;
    }
    return read(buffer, count);
}

enum class DecoderLibrary {
    None,
    SndFile,
    WavPack,
    Faad,
    Mad
};

// The library that can read fileName, tried in order of preference
static DecoderLibrary find_decoder(const QString& fileName)
{
    if (SFAudioReader::can_decode(fileName)) {
        return DecoderLibrary::SndFile;
    }
    if (WPAudioReader::can_decode(fileName)) {
        return DecoderLibrary::WavPack;
    }
#if defined M4A_DECODE_SUPPORT
    if (FaadAudioReader::can_decode(fileName)) {
        return DecoderLibrary::Faad;
    }
#endif
    if (MadAudioReader::can_decode(fileName)) {
        return DecoderLibrary::Mad;
    }
    return DecoderLibrary::None;
}

bool can_decode_audio(const QString& fileName)
{
    return find_decoder(fileName) != DecoderLibrary::None;
}

std::unique_ptr<TAudioDecoder> open_audio_decoder(const QString& fileName)
{
    std::unique_ptr<TAudioDecoder> decoder;

    switch (find_decoder(fileName)) {
    case DecoderLibrary::SndFile:
        decoder = std::make_unique<SFAudioReader>(fileName);
        break;
    case DecoderLibrary::WavPack:
        decoder = std::make_unique<WPAudioReader>(fileName);
        break;
    case DecoderLibrary::Faad:
#if defined M4A_DECODE_SUPPORT
        decoder = std::make_unique<FaadAudioReader>(fileName);
#endif
        break;
    case DecoderLibrary::Mad:
        decoder = std::make_unique<MadAudioReader>(fileName);
        break;
    case DecoderLibrary::None:
        PERROR(QString("File format not supported %1").arg(fileName));
        break;
    }

    if (decoder && !decoder->is_valid()) {
        PERROR(QString("new %1 decoder is invalid! (channels: %2, frames: %3)")
                   .arg(decoder->decoder_type())
                   .arg(decoder->channel_count())
                   .arg(decoder->frame_count()));
    }

    return decoder;
}
