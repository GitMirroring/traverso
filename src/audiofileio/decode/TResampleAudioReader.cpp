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
    m_fileFrames = file_to_resampled_frame(m_reader->get_nframes());
    m_length = TTimeRef(m_fileFrames, m_outputSampleRate);

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
    return m_reader->seek(resampled_to_file_frame(start));
}

nframes_t TResampleAudioReader::read_private(TFileIOBuffer& buffer, nframes_t frameCount)
{
    Q_ASSERT(m_reader);

    if (m_outputSampleRate == m_fileSampleRate || !m_isResampleAvailable) {
        return m_reader->read(buffer, frameCount);
    }

    nframes_t fileCnt = resampled_to_file_frame(frameCount);
    if (frameCount && !fileCnt) {
        fileCnt = 1;
    }

    // Prepare the temporary disk buffer to match the requested frame block size
    m_resampleDecodeBuffer.check_capacity(fileCnt, m_channels);
    m_resampleDecodeBuffer.silence_buffers();

    // Directly fetch raw data from the storage device
    nframes_t bytesRead = 0;
    if (!m_reader->eof()) {
        bytesRead = m_reader->read(m_resampleDecodeBuffer, fileCnt);
    }

    if (bytesRead == 0) {
        return 0;
    }

    nframes_t framesToConvert = frameCount;
    if (frameCount > m_fileFrames - m_readPos) {
        framesToConvert = m_fileFrames - m_readPos;
    }

    nframes_t framesRead = 0;

    for (uint chan = 0; chan < m_channels; chan++) {
        if (chan < m_resamplers.size() && m_resamplers[chan]) {
            nframes_t maxBufSize = m_resampleDecodeBuffer.get_channel_buffer(chan).get_size();

            framesRead = m_resamplers[chan]->process(
                m_resampleDecodeBuffer.get_channel_buffer(chan).get_data(maxBufSize),
                bytesRead,
                buffer.get_channel_buffer(chan).get_data(framesToConvert),
                framesToConvert,
                m_reader->eof()
                );
        }
    }

    if (framesRead > 0 && framesRead < framesToConvert) {
        nframes_t missing = framesToConvert - framesRead;
        for (uint chan = 0; chan < m_channels; chan++) {
            buffer.get_channel_buffer(chan).set_data_start_offset(framesRead);
            buffer.get_channel_buffer(chan).silence_data(missing);
            buffer.get_channel_buffer(chan).set_data_start_offset(0);
        }
        framesRead = framesToConvert;
    }

    if (m_readPos + framesRead > get_nframes()) {
        framesRead = get_nframes() - m_readPos;
    }

    return framesRead;
}
