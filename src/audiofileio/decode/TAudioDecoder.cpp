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

#include "TLog.h"

nframes_t TAudioDecoder::read_from(TFileIOBuffer& buffer, nframes_t first, nframes_t count)
{
    // At or past the end nothing to read, and no seek (contract point 2);
    // elsewhere a seek that fails leaves nothing to read either
    if (first >= m_frames || ((first != m_frame || m_error) && !seek(first))) {
        if (count > 0) {
            buffer.check_capacity(count, m_channels);
            buffer.silence(0, count, m_channels);
        }
        return 0;
    }
    return read(buffer, count);
}

bool TAudioDecoder::seek_done(nframes_t frame, bool landed)
{
    m_error = !landed;
    if (landed) {
        m_frame = frame;
    }
    return landed;
}

nframes_t TAudioDecoder::read_done(TFileIOBuffer& buffer, nframes_t read, nframes_t count)
{
    // What the file did not give: zeros (contract point 1)
    buffer.silence(read, count - read, m_channels);
    // Short before the end: the file ends early or a read failed (points 3, 4)
    if (read < count && m_frame + read < m_frames) {
        m_error = true;
    }
    m_frame += read;
    return read;
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
    if (const int format = SFAudioReader::major_format(fileName)) {
        // MP3 through MAD, whose seeks are sample accurate; libsndfile's are
        // not (1.2.2, mpg123 lands some hundreds of frames off)
        if (format == SF_FORMAT_MPEG && MadAudioReader::can_decode(fileName)) {
            return DecoderLibrary::Mad;
        }
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
        TLOG_W("audiofile", "{}: no decoder reads this format", fileName.toStdString());
        break;
    }

    if (decoder && !decoder->is_valid()) {
        TLOG_W("audiofile", "{}: the {} decoder cannot read it ({} channels, {} Hz, {} frames)", fileName.toStdString(),
               decoder->decoder_type().toStdString(), decoder->channel_count(), decoder->rate(), decoder->frame_count());
        return nullptr;
    }

    return decoder;
}
