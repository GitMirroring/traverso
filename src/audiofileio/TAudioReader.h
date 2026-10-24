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

#include "TAudioDecoder.h"
#include "TAudioResampler.h"
#include "TFileIOBuffer.h"

#include <memory>

/**
 * @brief The audio of one file at an output rate: a TAudioDecoder, and when
 * the output rate is not the file's, a TAudioResampler for all its channels.
 *
 * It keeps its own position, frame(), in frames at the output rate; at the
 * file's rate the decoder keeps its own. The length is the file's at any
 * output rate; output_frame_count() is the first output frame at or after
 * it, where the reader ends.
 *
 * read_from() keeps the contract of TAudioDecoder::read(): count frames of
 * every channel, what the file gives and zeros for the rest, also past the
 * end of the file; the caller never silences before a read. A resampled
 * read is sample exact: after a seek every output frame is where it belongs
 * on the file's timeline (test_resample_reader).
 *
 * Not for the audio thread: file IO, and the buffers may grow.
 */
class TAudioReader
{
public:
    explicit TAudioReader(const QString& fileName);
    ~TAudioReader();

    // Of the file, at its own rate
    bool is_valid() const { return m_decoder && m_decoder->is_valid(); }
    uint channel_count() const { return m_channels; }
    uint file_rate() const { return m_fileRate; }
    nframes_t file_frame_count() const { return m_decoder ? m_decoder->frame_count() : 0; }
    TTimeRef length() const { return m_decoder ? m_decoder->length() : TTimeRef(); }
    QString decoder_type() const { return m_decoder ? m_decoder->decoder_type() : QString(); }

    // At the output rate
    uint output_rate() const { return m_outputRate; }
    nframes_t output_frame_count() const { return m_outputFrames; }
    nframes_t frame() const { return m_frame; }

    // The output rate, and the resampler for it when it is not the file's
    // rate: a backend and one of its qualities (TAudioResampler::
    // qualities()). The reader is at frame() again, now at rate.
    void set_output_rate_and_resampler(uint rate, TAudioResampler::BackendType backend, int quality);

    // count frames at the output rate from frame first into buffer, as the
    // contract above says; the frames that came from the file. A seek only
    // when the reader is elsewhere.
    nframes_t read_from(TFileIOBuffer& buffer, nframes_t first, nframes_t count);
    // From the output frame nearest to first
    nframes_t read_from(TFileIOBuffer& buffer, const TTimeRef& first, nframes_t count) {
        return read_from(buffer, TTimeRef::to_frame(first, m_outputRate), count);
    }

private:
    // The reader at output frame frame: the decoder sought there, through
    // the resampler from the last frame of both rates before it; false when
    // the file cannot go there
    bool reposition(nframes_t frame);
    // The resampler and the frames to drop forget the position read before
    void reset();
    // count resampled frames from frame() into buffer; what came from the
    // file, zeros after it
    nframes_t read_resampled(TFileIOBuffer& buffer, nframes_t count);
    // count output frames into out from frame 0, reading the file as far as
    // that takes; fewer only at the end of the file or on a read error
    nframes_t resample_into(TFileIOBuffer& out, nframes_t count);
    // The file frames that cover outputFrames output frames: one read of
    // the decoder for them
    nframes_t file_frames_for(nframes_t outputFrames) const {
        return TTimeRef::to_nframes(TTimeRef::frames_covering(outputFrames, m_outputRate, m_fileRate));
    }

    std::unique_ptr<TAudioDecoder> m_decoder;
    // Only when the output rate is not the file's
    std::unique_ptr<TAudioResampler> m_resampler;
    // What the decoder reads for the resampler
    TFileIOBuffer m_decodeBuffer;

    uint m_channels{0};
    uint m_fileRate{0};
    uint m_outputRate{0};
    nframes_t m_outputFrames{0};
    nframes_t m_frame{0};
    // After a seek: output frames to drop before the frame asked for
    nframes_t m_dropFrames{0};
};
