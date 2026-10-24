/*
Copyright (C) 2006-2026 Ben Levitt, Remon Sijrier

This file is part of Traverso

Traverso is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA.

*/


#pragma once

#include "TAudioResampler.h"
#include "TFileIOBuffer.h"
#include "TTimeRef.h"
#include "defines.h"

#include <QString>

#include <memory>

class TAudioDecoder;

/**
 * @brief The audio of one file: the one way to read an audio file in
 * Traverso (TAudioWriter is the one way to write one).
 *
 * It opens the file with the decoder for its format (libsndfile, WavPack,
 * MP3, AAC) and gives the file's facts: channels, rate, frames, length. The
 * audio comes two ways, from the same reader, side by side:
 * - read(): at the output rate, from a location in time. With the output
 *   rate the file's own, the file as it is; with another, through a
 *   TAudioResampler for all channels, sample exact: every output frame is
 *   where it belongs on the file's timeline (test_resample_reader).
 * - read_file_frames(): the file's own frames, at its own rate, whatever the
 *   output rate: for what works on the file's grid (peaks, a waveform).
 *
 * Both keep the contract of TAudioDecoder::read(): count frames of every
 * channel are filled, what the file gives and zeros for the rest, also past
 * the end of the file; the caller never silences before a read.
 *
 * Positions come in as time (TTimeRef) or as the file's frames; the frames
 * at the output rate stay inside. Not for the audio thread: file IO, and the
 * buffers may grow.
 */
class TAudioReader
{
public:
    // Whether a decoder reads fileName: a probe, nothing is kept or logged
    static bool can_read(const QString& fileName);

    explicit TAudioReader(const QString& fileName);
    ~TAudioReader();

    // The file, as its decoder knows it
    bool is_valid() const;
    uint channel_count() const;
    uint file_rate() const;
    nframes_t file_frame_count() const;
    TTimeRef length() const;
    QString decoder_type() const;

    // The output rate of read(), and the resampler for it when it is not the
    // file's rate: a backend and one of its qualities
    // (TAudioResampler::qualities()). The reader stays where it was, now at
    // rate. Until it is called, the output rate is the file's.
    void set_output_rate_and_resampler(uint rate, TAudioResampler::BackendType backend, int quality);
    uint output_rate() const { return m_outputRate; }
    // The frames the file gives at the output rate: the first output frame at
    // or after its length, where read() ends
    nframes_t output_frame_count() const { return m_outputFrames; }

    // count frames at the output rate into buffer, from the output frame at
    // or before location (frame n sits at n / rate); the frames that came
    // from the file. A seek only when the reader is elsewhere.
    nframes_t read(TFileIOBuffer& buffer, const TTimeRef& location, nframes_t count);
    // count frames of the file at its own rate into buffer, from its frame
    // firstFrame; the frames that came from the file
    nframes_t read_file_frames(TFileIOBuffer& buffer, nframes_t firstFrame, nframes_t count);

private:
    // The reader at output frame frame: the decoder sought there, through
    // the resampler from the last frame of both rates before it; false when
    // the file cannot go there
    bool reposition(nframes_t frame);
    // The resampler and the frames to drop forget the position read before
    void reset();
    // count resampled frames from the reader's position into buffer; what
    // came from the file, zeros after it
    nframes_t read_resampled(TFileIOBuffer& buffer, nframes_t count);
    // count output frames into out from frame 0, reading the file as far as
    // that takes; fewer only at the end of the file or on a read error
    nframes_t resample_into(TFileIOBuffer& out, nframes_t count);
    // The file frames that cover outputFrames output frames: one read of
    // the decoder for them
    nframes_t file_frames_for(nframes_t outputFrames) const;

    std::unique_ptr<TAudioDecoder> m_decoder;
    // Only when the output rate is not the file's
    std::unique_ptr<TAudioResampler> m_resampler;
    // What the decoder reads for the resampler
    TFileIOBuffer m_decodeBuffer;

    uint m_outputRate{0};
    nframes_t m_outputFrames{0};
    // The position of read(), in output frames
    nframes_t m_frame{0};
    // A read_file_frames() moved the decoder: the next read() puts it back
    // on the frame where the output left it (the resampler is untouched, so
    // the output goes on as one read would)
    bool m_decoderMoved{false};
    nframes_t m_resumeFrame{0};
    // After a seek: output frames to drop before the frame asked for
    nframes_t m_dropFrames{0};
};
