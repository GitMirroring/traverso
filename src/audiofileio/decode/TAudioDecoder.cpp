// SPDX-FileCopyrightText: 2007-2026 Ben Levitt, Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TAudioDecoder.h"

#include "TFileIOBuffer.h"

#include "SFAudioReader.h"
#include "WPAudioReader.h"
#include "MadAudioReader.h"
#include "TScenarioDecoder.h"

#if defined M4A_DECODE_SUPPORT
#include "FaadAudioReader.h"
#endif

#include <QFile>
#include <QFileInfo>

TAudioFileResult TAudioDecoder::read_from(TFileIOBuffer& buffer, nframes_t first, nframes_t count)
{
    // Nothing to read: zeros, as read() gives past the end
    const auto nothing = [&](std::optional<TAudioFileError> error) -> TAudioFileResult {
        if (count > 0) {
            buffer.check_capacity(count, m_channels);
            buffer.silence(0, count, m_channels);
        }
        return {0, std::move(error)};
    };

    // At or past the end: no seek, no error (contract point 2)
    if (first >= m_frames) {
        return nothing(std::nullopt);
    }
    // A seek only when the decoder does not read from first already
    if (!reads_from(first)) {
        if (TAudioFileStatus sought = seek(first); !sought) {
            return nothing(std::move(sought.error()));
        }
    }
    return read(buffer, count);
}

TAudioFileStatus TAudioDecoder::seek_landed(nframes_t frame)
{
    m_frame = frame;
    m_error = false;
    return {};
}

TAudioFileStatus TAudioDecoder::seek_failed(nframes_t frame, const QString& detail)
{
    m_error = true;
    return std::unexpected(TAudioFileError{TAudioFileError::Code::SeekFailed,
                                           detail.isEmpty() ? QStringLiteral("cannot seek to frame %1").arg(frame) : detail});
}

TAudioFileResult TAudioDecoder::read_done(TFileIOBuffer& buffer, nframes_t read, nframes_t count, const QString& detail)
{
    // What the file did not give: zeros (contract point 1)
    buffer.silence(read, count - read, m_channels);
    TAudioFileResult result{read, std::nullopt};
    // Short before the end: the file ends early or a read failed (points 3, 4)
    if (read < count && m_frame + read < m_frames) {
        m_error = true;
        result.error = TAudioFileError{TAudioFileError::Code::ReadFailed,
                                       detail.isEmpty() ? QStringLiteral("the audio ends at frame %1 of %2").arg(m_frame + read).arg(m_frames) : detail};
    }
    m_frame += read;
    return result;
}

enum class DecoderLibrary {
    None,
    Scenario,
    SndFile,
    WavPack,
    Faad,
    Mad
};

// The library that can read fileName, tried in order of preference
static DecoderLibrary find_decoder(const QString& fileName)
{
    // A scenario (a test signal described, TScenarioDecoder) by its name,
    // before any library probes it; one that does not parse is no audio file.
    // First, because the libraries probe the bytes: MAD looks for frame
    // syncs and can find one in any text, so a scenario (or a broken one)
    // could pass as an MP3. Cheap for every other file: only the name is
    // compared, the file is opened only when it is called .tscenario.
    if (TScenarioDecoder::is_scenario_file(fileName)) {
        return TScenarioDecoder::check(fileName) ? DecoderLibrary::Scenario : DecoderLibrary::None;
    }
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

// Why no decoder reads fileName: not there, not readable, or of a format no
// decoder knows. Asked after the probes failed, not before: a check before
// the open is a race and costs a system call on every open.
static TAudioFileError no_decoder(const QString& fileName)
{
    if (!QFileInfo::exists(fileName)) {
        return {TAudioFileError::Code::NotFound, QStringLiteral("no such file")};
    }
    if (QFile file(fileName); !file.open(QIODevice::ReadOnly)) {
        return {TAudioFileError::Code::CannotOpen, file.errorString()};
    }
    if (TScenarioDecoder::is_scenario_file(fileName)) {
        if (std::expected<void, QString> scenario = TScenarioDecoder::check(fileName); !scenario) {
            return {TAudioFileError::Code::CannotOpen, QStringLiteral("not a scenario: %1").arg(scenario.error())};
        }
    }
    return {TAudioFileError::Code::CannotOpen, QStringLiteral("no decoder reads this format")};
}

std::expected<std::unique_ptr<TAudioDecoder>, TAudioFileError> open_audio_decoder(const QString& fileName)
{
    std::unique_ptr<TAudioDecoder> decoder;

    switch (find_decoder(fileName)) {
    case DecoderLibrary::Scenario:
        decoder = std::make_unique<TScenarioDecoder>(fileName);
        break;
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
        return std::unexpected(no_decoder(fileName));
    }

    if (!decoder || !decoder->is_valid()) {
        // The decoder's own reason, else what it found (a file that went
        // between the probe and the open lands here too)
        if (decoder && decoder->open_error()) {
            return std::unexpected(*decoder->open_error());
        }
        return std::unexpected(TAudioFileError{TAudioFileError::Code::CannotOpen,
                                               decoder ? QStringLiteral("the %1 decoder cannot read it (%2 channels, %3 Hz, %4 frames)")
                                                             .arg(decoder->decoder_type())
                                                             .arg(decoder->channel_count())
                                                             .arg(decoder->rate())
                                                             .arg(decoder->frame_count())
                                                       : QStringLiteral("no decoder reads this format")});
    }

    return decoder;
}
