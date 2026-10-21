/*
Copyright (C) 2007 - 2026 Ben Levitt, Remon Sijrier

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


#include "ResampleAudioReader.h"
#include "TAudioResampler.h"
#include "TAudioBuffer.h"
#include "Debugger.h"
#include <samplerate.h>

#include <algorithm>

TResampleAudioReader::TResampleAudioReader(const QString& filename)
    : AbstractAudioReader(filename)
{
    m_reader = AbstractAudioReader::create_audio_reader(filename);
    if (!m_reader) {
        PERROR("ResampleAudioReader: couldn't create AudioReader");
        m_channels = m_fileFrames = 0;
    } else {
        m_channels = m_reader->get_num_channels();
        m_fileSampleRate = m_reader->get_file_rate();
        m_fileFrames = m_reader->get_nframes();
        m_length = m_reader->get_length();
        m_outputSampleRate = m_fileSampleRate;
    }

    m_isResampleAvailable = false;
    m_convertorType = -1;
}

TResampleAudioReader::~TResampleAudioReader()
{
}

void TResampleAudioReader::clear_buffers()
{
    if (m_reader) {
        m_reader->clear_buffers();
    }
}

void TResampleAudioReader::reset()
{
    for (const auto& resampler : m_resamplers) {
            resampler->reset();
    }
    m_dropFrames = 0;
}

void TResampleAudioReader::set_converter_type(int converterType)
{
    PENTER;

    if ((float(m_outputSampleRate) / get_file_rate()) > 2.0f &&
        (converterType == SRC_ZERO_ORDER_HOLD || converterType == SRC_LINEAR)) {
        m_convertorType = SRC_SINC_FASTEST;
    } else {
        m_convertorType = converterType;
    }

    m_resamplers.clear();
    clear_buffers();

    m_isResampleAvailable = true;
    double ratio = double(m_outputSampleRate) / m_fileSampleRate;

    for (uint c = 0; c < m_channels; c++) {
        m_resamplers.push_back(std::make_unique<TAudioResampler>(TAudioResampler::BackendType::LIBSOXR, ratio, m_convertorType, 512));
    }

    seek_private(pos());
}

void TResampleAudioReader::set_output_rate(uint rate)
{
    if (!m_reader) return;
    if (m_outputSampleRate == rate) return;

    m_outputSampleRate = rate;
    // The length is the file's, at any output rate; the frames the file
    // gives at the output rate end where it ends
    m_length = m_reader->get_length();
    m_fileFrames = rate == m_fileSampleRate ? m_reader->get_nframes()
                                            : nframes_t(TTimeRef::frame_at_or_after(m_length, m_outputSampleRate));

    double ratio = double(m_outputSampleRate) / m_fileSampleRate;
    for (const auto& resampler : m_resamplers) {
        resampler->set_ratio(ratio);
    }

    reset();
}

bool TResampleAudioReader::seek_private(nframes_t start)
{
    Q_ASSERT(m_reader);
    if (m_outputSampleRate == m_fileSampleRate || !m_isResampleAvailable) {
        return m_reader->seek(start);
    }
    reset();

    // A position in output frames is mostly no whole frame of the file, and
    // a seek to the nearest file frame would shift the output by a fraction
    // of a frame. The file is sought to the last frame before start where a
    // frame of both rates coincide (every 147 frames of 44.1 kHz and 160 of
    // 48 kHz); the next read drops the output frames from there to start.
    const TTimeRef position(start, m_outputSampleRate);
    const TTimeRef common = TTimeRef::common_frame_at_or_before(position, m_fileSampleRate, m_outputSampleRate);
    m_dropFrames = nframes_t(TTimeRef::frame_at_or_before(position - common, m_outputSampleRate));
    return m_reader->seek(nframes_t(TTimeRef::frame_at_or_before(common, m_fileSampleRate)));
}

nframes_t TResampleAudioReader::resample_turn(TFileIOBuffer& out, nframes_t at, nframes_t count, nframes_t& inputFrames)
{
    nframes_t fileCnt = resampled_to_file_frame(count);
    if (!fileCnt) {
        fileCnt = 1;
    }

    inputFrames = 0;
    if (!m_reader->eof()) {
        m_resampleDecodeBuffer.check_capacity(fileCnt, m_channels);
        m_resampleDecodeBuffer.silence_buffers();
        inputFrames = m_reader->read(m_resampleDecodeBuffer, fileCnt);
    }
    const bool endOfInput = m_reader->eof();

    nframes_t generated = 0;
    for (uint chan = 0; chan < m_channels && chan < m_resamplers.size(); chan++) {
        float* output = out.get_channel_buffer(chan).get_data(at + count) + at;
        if (inputFrames == 0 && endOfInput) {
            // The file is read to its end: what the resampler still holds
            generated = m_resamplers[chan]->flush(output, count);
            continue;
        }
        const nframes_t inputSize = m_resampleDecodeBuffer.get_channel_buffer(chan).get_size();
        generated = m_resamplers[chan]->process(m_resampleDecodeBuffer.get_channel_buffer(chan).get_data(inputSize),
                                                inputFrames, output, count, endOfInput);
    }
    return generated;
}

nframes_t TResampleAudioReader::resample(TFileIOBuffer& out, nframes_t count)
{
    // The resampler gives what its input allows: after a seek it holds back
    // the length of its filter, and soxr releases its output in blocks of its
    // own. So it is fed until it gave all that was asked for, reading as much
    // of the file as that takes; once the file is read to its end, it is
    // flushed.
    nframes_t produced = 0;
    while (produced < count) {
        nframes_t inputFrames = 0;
        const nframes_t generated = resample_turn(out, produced, count - produced, inputFrames);
        produced += generated;
        if (inputFrames == 0 && !m_reader->eof()) {
            // A read error
            break;
        }
        // At the end of the file the resampler is flushed until it gives
        // nothing
        if (generated == 0 && inputFrames == 0) {
            break;
        }
    }
    return produced;
}

nframes_t TResampleAudioReader::read_private(TFileIOBuffer& buffer, nframes_t frameCount)
{
    Q_ASSERT(m_reader);

    if (m_outputSampleRate == m_fileSampleRate || !m_isResampleAvailable) {
        return m_reader->read(buffer, frameCount);
    }

    nframes_t framesToConvert = frameCount;
    if (frameCount > m_fileFrames - m_readPos) {
        framesToConvert = m_fileFrames - m_readPos;
    }
    if (framesToConvert == 0) {
        return 0;
    }

    // After a seek: the output frames between the common frame the file was
    // sought to and the position asked for (seek_private()), in pieces the
    // buffer holds; the output asked for writes over them
    while (m_dropFrames) {
        const nframes_t piece = std::min(m_dropFrames, framesToConvert);
        m_dropFrames = resample(buffer, piece) < piece ? 0 : m_dropFrames - piece;
    }

    // Silence only past the end of what the resampler gives: padding a short
    // read anywhere else would shift all that follows
    const nframes_t produced = resample(buffer, framesToConvert);
    if (produced < framesToConvert) {
        for (uint chan = 0; chan < m_channels; chan++) {
            buffer.get_channel_buffer(chan).set_data_start_offset(produced);
            buffer.get_channel_buffer(chan).silence_data(framesToConvert - produced);
            buffer.get_channel_buffer(chan).set_data_start_offset(0);
        }
    }

    return framesToConvert;
}
