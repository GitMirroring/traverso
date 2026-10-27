// SPDX-FileCopyrightText: 2007-2026 Ben Levitt, Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "TTimeRef.h"
#include "defines.h"

#include <QString>

#include <memory>

class TFileIOBuffer;

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
 *    seek tolerance, AAC). A seek that cannot go there returns false, leaves
 *    frame() unchanged and is an error.
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
 * Each decoder ends its seek() with seek_done() and its read() with
 * read_done(), which keep points 3 and 4.
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
    // The library, for messages
    virtual QString decoder_type() const = 0;

    // The frame the next read() starts at
    nframes_t frame() const { return m_frame; }
    bool at_end() const { return m_frame >= m_frames; }
    // A seek or a read failed, and no seek succeeded since (point 3)
    bool error() const { return m_error; }

    // To frame, which is below frame_count(); false when the decoder cannot
    // go there (frame() is then unchanged)
    virtual bool seek(nframes_t frame) = 0;
    // count frames from frame() into buffer, as the contract above says;
    // the frames read from the file, and frame() moves on by as many
    virtual nframes_t read(TFileIOBuffer& buffer, nframes_t count) = 0;

    // count frames from first: a seek only when frame() is elsewhere or the
    // decoder is in its error state (a seek costs: MAD and FAAD decode
    // frames before the target), then read(). At or past the end, and where
    // the decoder cannot seek to, zeros and 0.
    nframes_t read_from(TFileIOBuffer& buffer, nframes_t first, nframes_t count);

protected:
    explicit TAudioDecoder(const QString& fileName) : m_fileName(fileName) {}

    // The end of every seek(): at frame and no error when it landed; an error
    // and frame() unchanged when not. Returns landed.
    bool seek_done(nframes_t frame, bool landed);
    // The end of every read(): zeros past the frames read, frame() moved on
    // by them, and fewer frames than asked before frame_count() an error.
    // Returns read.
    nframes_t read_done(TFileIOBuffer& buffer, nframes_t read, nframes_t count);

    // Set by the decoder when it opens the file
    QString m_fileName;
    uint m_channels{0};
    uint m_rate{0};
    nframes_t m_frames{0};
    // Moved by the decoder's seek() and read() only, through seek_done() and
    // read_done()
    nframes_t m_frame{0};
    bool m_error{false};
};

// The decoder for fileName, open and valid; nullptr when none can read it
// (logged). A scenario file (.tscenario) is known by its name and read by
// TScenarioDecoder; other files go to the libraries in order of preference
// (libsndfile, WavPack, FAAD, MAD).
std::unique_ptr<TAudioDecoder> open_audio_decoder(const QString& fileName);

// Whether one of the decoders can read fileName. The file is only probed: no
// decoder is kept, nothing is logged.
bool can_decode_audio(const QString& fileName);
