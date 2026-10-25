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
 * MP3 through MAD, FaadAudioReader: AAC), opened by open_audio_decoder().
 *
 * What a decoder does, and nothing else does for it: it keeps its position
 * in the file (frame(), in frames at the file's rate), moves it (seek()) and
 * reads from it (read()). The file's channels, rate and frames are set once,
 * when it is opened.
 *
 * The contract of read(), which every decoder keeps and test_audio_decoders
 * checks: count frames of every channel are filled, from frame 0 of the
 * buffer's channel buffers: what the file gives, zeros for the rest, also at
 * or past the end of the file. A caller never silences a buffer before a
 * read, and never sees what the buffer held before: no stale audio reaches
 * a ring buffer or a file.
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

    // To frame, which is below frame_count(); false when the decoder cannot
    // go there (frame() is then unchanged)
    virtual bool seek(nframes_t frame) = 0;
    // count frames from frame() into buffer, as the contract above says;
    // the frames read from the file, and frame() moves on by as many
    virtual nframes_t read(TFileIOBuffer& buffer, nframes_t count) = 0;

    // count frames from first: a seek only when frame() is elsewhere (a
    // seek costs, and a lossy format may not land on the frame exactly),
    // then read(). A first the decoder cannot seek to gives zeros.
    nframes_t read_from(TFileIOBuffer& buffer, nframes_t first, nframes_t count);

protected:
    explicit TAudioDecoder(const QString& fileName) : m_fileName(fileName) {}

    // Set by the decoder when it opens the file
    QString m_fileName;
    uint m_channels{0};
    uint m_rate{0};
    nframes_t m_frames{0};
    // Moved by the decoder's seek() and read() only
    nframes_t m_frame{0};
};

// The decoder for fileName, the libraries tried in order of preference
// (libsndfile, WavPack, FAAD, MAD), open and valid; nullptr when none can
// read it (logged)
std::unique_ptr<TAudioDecoder> open_audio_decoder(const QString& fileName);

// Whether one of the decoders can read fileName. The file is only probed: no
// decoder is kept, nothing is logged.
bool can_decode_audio(const QString& fileName);
