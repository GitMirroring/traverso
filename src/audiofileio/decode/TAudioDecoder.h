// SPDX-FileCopyrightText: 2007-2026 Ben Levitt, Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "TAudioFileError.h"
#include "TTimeRef.h"
#include "defines.h"

#include <QString>

#include <expected>
#include <memory>

class TAudioBuffer;

/**
 * @brief Reads the audio of one file at the file's own rate. One class per
 * library (SFAudioReader: libsndfile, WPAudioReader: WavPack, MadAudioReader:
 * MP3 through MAD, FaadAudioReader: AAC), and TScenarioDecoder for a test
 * signal described in a scenario file; opened by open_audio_decoder().
 *
 * What a decoder does, and nothing else does for it: it keeps its position
 * in the file (frame(), in frames at the file's rate), moves it (seek()) and
 * reads from it (read()). The file's channels, rate and frames are set once,
 * when it is opened.
 *
 * The contract, which every decoder keeps and test_audio_decoders checks:
 * 1. read(buffer, count) fills count frames of every channel, from frame 0 of
 *    the buffer's channel buffers: what the file gives, zeros for the rest.
 *    It gives fewer frames than asked only at the end of the file or on an
 *    error.
 * 2. seek(frame) is only asked for a frame below frame_count(); a read at or
 *    past the end is handled above the decoder (read_from(),
 *    TAudioReader::read()): zeros and 0, no decoder call, no error. A seek
 *    that succeeds lands exactly: frame() is frame and the next read starts
 *    there, its samples equal to a read from the start (within the format's
 *    seek tolerance, AAC). A seek that cannot go there says why (SeekFailed),
 *    leaves frame() unchanged and is an error.
 * 3. On an error (a failed seek, a failed read) the decoder is in its error
 *    state (error()): reads give zeros and 0 until a seek succeeds, which
 *    clears it, so a damaged part of a file can be skipped. A caller seeks
 *    again when the decoder is in its error state, also to frame().
 * 4. Audio that ends before frame_count() (a header that says more, a file
 *    cut short) is an error, where the decoder does not repair it itself
 *    (MAD and the LAME padding). A packet that does not decode (MAD, FAAD)
 *    stays silence, as the decoder decides: not an error.
 * 5. No stale audio: a caller never silences a buffer before a read, and
 *    never sees what the buffer held before; none reaches a ring buffer or
 *    a file.
 *
 * Each decoder ends its seek() with seek_landed() or seek_failed() and its
 * read() with read_done(), which keep points 3 and 4 and say why a call
 * failed (TAudioFileError): the error of that call, nothing is logged. A
 * decoder that cannot open its file says why in m_openError.
 *
 * Not for the audio thread: file IO, and the buffer may grow.
 */
class TAudioDecoder
{
public:
    virtual ~TAudioDecoder() = default;

    // Of the file, at its own rate. No channels or no frames: it could not
    // be read
    const QString& file_name() const { return m_fileName; }
    uint channel_count() const { return m_channels; }
    uint rate() const { return m_rate; }
    nframes_t frame_count() const { return m_frames; }
    TTimeRef length() const { return TTimeRef(m_frames, m_rate); }
    // Channels, a rate and frames: a decoder without one of them reads
    // nothing (and a rate of 0 would be divided by)
    bool is_valid() const { return m_channels > 0 && m_rate > 0 && m_frames > 0; }
    // Why it could not open its file, when it said so
    const std::optional<TAudioFileError>& open_error() const { return m_openError; }
    // The library, for messages
    virtual QString decoder_type() const = 0;

    // The frame the next read() starts at
    nframes_t frame() const { return m_frame; }
    bool at_end() const { return m_frame >= m_frames; }
    // A seek or a read failed, and no seek succeeded since (point 3)
    bool error() const { return m_error; }
    // The next read() reads frame: the decoder is there and not in its
    // error state. Otherwise a read from frame seeks first (point 3).
    bool reads_from(nframes_t frame) const { return m_frame == frame && !m_error; }

    // To frame, which is below frame_count(); why not when the decoder
    // cannot go there (frame() is then unchanged)
    [[nodiscard]] virtual TAudioFileStatus seek(nframes_t frame) = 0;
    // count frames from frame() into buffer, as the contract above says:
    // the frames read from the file (frame() moves on by as many), and the
    // error when this read failed
    virtual TAudioFileResult read(TAudioBuffer& buffer, nframes_t count) = 0;

    // count frames from first: a seek only when frame() is elsewhere or the
    // decoder is in its error state (a seek costs: MAD and FAAD decode
    // frames before the target), then read(). At or past the end zeros and
    // 0, no error; where the decoder cannot seek to, zeros, 0 and the seek's
    // error.
    TAudioFileResult read_from(TAudioBuffer& buffer, nframes_t first, nframes_t count);

protected:
    explicit TAudioDecoder(const QString& fileName) : m_fileName(fileName) {}

    // The end of every seek() that landed: at frame, no error
    TAudioFileStatus seek_landed(nframes_t frame);
    // The end of every seek() that did not: the error state, frame()
    // unchanged; detail says why (the library's words where it has any)
    TAudioFileStatus seek_failed(nframes_t frame, const QString& detail = QString());
    // The end of every read(): zeros past the frames read, frame() moved on
    // by them, and fewer frames than asked before frame_count() an error,
    // with detail where the decoder knows why
    TAudioFileResult read_done(TAudioBuffer& buffer, nframes_t read, nframes_t count, const QString& detail = QString());

    // Set by the decoder when it opens the file
    QString m_fileName;
    uint m_channels{0};
    uint m_rate{0};
    nframes_t m_frames{0};
    // Moved by the decoder's seek() and read() only, through seek_landed(),
    // seek_failed() and read_done()
    nframes_t m_frame{0};
    bool m_error{false};
    // Set by a decoder that cannot open its file, with the reason
    std::optional<TAudioFileError> m_openError;
};

// The decoder for fileName, open and valid, or why there is none: NotFound
// when the file is not there, else CannotOpen (no permission, no decoder for
// its format, the decoder's own reason). A scenario file (.tscenario) is
// known by its name and read by TScenarioDecoder; other files go to the
// libraries in order of preference (libsndfile, WavPack, FAAD, MAD).
[[nodiscard]] std::expected<std::unique_ptr<TAudioDecoder>, TAudioFileError> open_audio_decoder(const QString& fileName);

// Whether one of the decoders can read fileName. The file is only probed: no
// decoder is kept, nothing is logged.
bool can_decode_audio(const QString& fileName);
