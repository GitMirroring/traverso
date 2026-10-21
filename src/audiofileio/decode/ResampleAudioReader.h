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

#include "TFileIOBuffer.h"
#include "AbstractAudioReader.h"
#include "TAudioResampler.h"
#include <memory>
#include <vector>

class TResampleAudioReader : public AbstractAudioReader
{
public:
    TResampleAudioReader(const QString &filename);
    ~TResampleAudioReader();

    nframes_t read_from(TFileIOBuffer& buffer, nframes_t start, nframes_t count) {
        return AbstractAudioReader::read_from(buffer, start, count);
    }
    nframes_t read_from(TFileIOBuffer& buffer, const TTimeRef& start, nframes_t count) {
        return AbstractAudioReader::read_from(buffer, TTimeRef::to_frame(start, m_outputSampleRate), count);
    }

    QString decoder_type() const { return (m_reader) ? m_reader->decoder_type() : ""; }
    void clear_buffers();

    uint get_output_rate() { return m_outputSampleRate; }
    uint get_file_rate() { return m_reader->get_file_rate(); }
    // The frames of the file at its own rate; get_nframes() counts them at
    // the output rate
    nframes_t get_file_frames() const { return m_reader ? m_reader->get_nframes() : 0; }
    void set_output_rate(uint rate);
    // The resampler for another output rate than the file's: a backend and
    // one of its qualities (TAudioResampler::qualities())
    void set_resampler(TAudioResampler::BackendType backend, int quality);

protected:
    void reset();
    bool seek_private(nframes_t start) final;
    nframes_t read_private(TFileIOBuffer& buffer, nframes_t frameCount) final;
    // count output frames into out, reading the file as far as that takes;
    // fewer only at the end of the file or on a read error
    nframes_t resample(TFileIOBuffer& out, nframes_t count);
    // One feed of the resampler with the input for count output frames;
    // what it gives goes into out from frame at
    nframes_t resample_turn(TFileIOBuffer& out, nframes_t at, nframes_t count, nframes_t& inputFrames);

    nframes_t resampled_to_file_frame(nframes_t frame) {
        return TTimeRef::to_frame(TTimeRef(frame, m_outputSampleRate), m_fileSampleRate);
    }

    std::unique_ptr<AbstractAudioReader>            m_reader;
    std::vector<std::unique_ptr<TAudioResampler>>   m_resamplers;

    uint                    m_outputSampleRate;
    TAudioResampler::BackendType m_backend{TAudioResampler::BackendType::LIBSOXR};
    int                     m_quality{0};
    bool                    m_isResampleAvailable;

private:
    // One per channel, for the ratio of the output rate to the file's
    void create_resamplers();

    TFileIOBuffer m_resampleDecodeBuffer;
    // After a seek: output frames to drop before the position asked for
    nframes_t m_dropFrames{0};
};

