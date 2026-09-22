/*
Copyright (C) 2007 Ben Levitt 

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
#include "TAudioBuffer.h"
#include "Debugger.h"
#include <samplerate.h>

const long OVERFLOW_SIZE = 512;


struct PrivateSRC {
    QList<SRC_STATE*>	srcStates;
    SRC_DATA            srcData{};
};


// On init, creates a child AudioReader for any filetype, and a samplerate converter
ResampleAudioReader::ResampleAudioReader(const QString& filename)
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

    m_privateSRC = std::make_unique<PrivateSRC>();
	m_isResampleAvailable = false;
	m_overflowUsed = 0;
	m_convertorType = -1;
}


ResampleAudioReader::~ResampleAudioReader()
{
    while (m_privateSRC->srcStates.size()) {
        src_delete(m_privateSRC->srcStates.back());
        m_privateSRC->srcStates.pop_back();
	}
}


void ResampleAudioReader::clear_buffers()
{
    for (const auto & buffer : m_overflowBuffers)  {
        buffer->silence_data();
    }

	if (m_reader) {
		m_reader->clear_buffers();
	}
}

// Clear the samplerateconverter to a clean state (used on seek)
void ResampleAudioReader::reset()
{
    for(SRC_STATE* state : std::as_const(m_privateSRC->srcStates)) {
		src_reset(state);
	}
	
    m_privateSRC->srcData.end_of_input = 0;
	m_overflowUsed = 0;
	
	// Read extra frames from the child reader on the first read after a seek.
	// This keeps the resampler supplied with plenty of samples to produce the 
	// requested output on each read.
    m_readExtraFrames = OVERFLOW_SIZE;
    // Linear conversion can return more samples then requested
    // so give the overflow buffers some spare room if that happens
    // for now, only 1 extra sample has been observed being produced by src
    // when converting from 44.1 to 48.0 KHz
    m_readExtraFrames -= 64;
}

void ResampleAudioReader::set_converter_type(int converterType)
{
	PENTER;

	int error;

    if ( (float(m_outputSampleRate) / get_file_rate()) > 2.0f && (converterType == SRC_ZERO_ORDER_HOLD || converterType == SRC_LINEAR) ) {
        printf("ResampleAudioReader::set_converter_type: src does not support a resample ratio > 2 with converter type Fast, using quality SINC FASTEST\n");
        m_convertorType = SRC_SINC_FASTEST;
	} else {
        m_convertorType = converterType;
	}
	
    while (m_privateSRC->srcStates.size()) {
        src_delete(m_privateSRC->srcStates.back());
        m_privateSRC->srcStates.pop_back();
	}
	
	clear_buffers();
	
    for (uint c = 0; c < m_reader->get_num_channels(); c++) {
		
        m_privateSRC->srcStates.append(src_new(m_convertorType, 1, &error));
		
        if (!m_privateSRC->srcStates[c]) {
			PERROR("ResampleAudioReader: couldn't create libSampleRate SRC_STATE");
			m_isResampleAvailable = false;
			break;
		} else {
			m_isResampleAvailable = true;
		}
	}
	
	// seek_private will reset the src states!
	seek_private(pos());
}

uint ResampleAudioReader::get_output_rate()
{
    return m_outputSampleRate;
}

uint ResampleAudioReader::get_file_rate()
{
	return m_reader->get_file_rate();
}

/* Note: Always call set_converter_type() after callling this function
 * it is needed for internal reasons
*/
void ResampleAudioReader::set_output_rate(uint rate)
{
	if (!m_reader) {
		return;
	}
    if (m_outputSampleRate == rate) {
        return;
    }
    m_outputSampleRate = rate;
    m_fileFrames = file_to_resampled_frame(m_reader->get_nframes());
    m_length = TTimeRef(m_fileFrames, m_outputSampleRate);
	
	reset();
}


// if no conversion is necessary, pass the seek straight to the child AudioReader,
// otherwise convert and seek
bool ResampleAudioReader::seek_private(nframes_t start)
{
	Q_ASSERT(m_reader);
	
    if (m_outputSampleRate == m_fileSampleRate || !m_isResampleAvailable) {
		return m_reader->seek(start);
	}
	
	reset();
// 	printf("ResampleAudioReader::seek_private: start: %d\n", resampled_to_file_frame(start));
	return m_reader->seek(resampled_to_file_frame(start));
}


// If no conversion is necessary, pass the read straight to the child AudioReader,
// otherwise get data from childreader and use libsamplerate to convert
nframes_t ResampleAudioReader::read_private(TFileDecodeBuffer& buffer, nframes_t frameCount)
{
	Q_ASSERT(m_reader);
	
	// pass through if not changing sampleRate.
    if (m_outputSampleRate == m_fileSampleRate || !m_isResampleAvailable) {
		return m_reader->read(buffer, frameCount);
    } else if (m_overflowBuffers.size() == 0) {
		create_overflow_buffers();
	}

    long bufferUsed;
	nframes_t framesRead = 0;
	
	nframes_t fileCnt = resampled_to_file_frame(frameCount);
	
	if (frameCount && !fileCnt) {
		fileCnt = 1;
	}
	
    if (m_resampleDecodeBuffer.get_destination_buffer_size() == 0) {
		reset();
	}

    bufferUsed = m_overflowUsed;
	
	if (m_overflowUsed) {
		// Copy pre-existing overflow into the buffer
        for (uint chan = 0; chan < m_channels; chan++) {
            TAudioBuffer::copy_data(m_resampleDecodeBuffer.get_destination_buffer(chan), *m_overflowBuffers.at(chan), m_overflowUsed);
		}
	}
		
	if (!m_reader->eof()) {
		if (m_overflowUsed) {
            m_resampleDecodeBuffer.set_destination_buffer_read_offset(m_overflowUsed);
		}

        long toRead = fileCnt + m_readExtraFrames - m_overflowUsed;
        // It happened that fileCnt + m_readExtraFrames was smaller then m_overflowUsed
        // since nframes_t was used in the m_reader->read() function it wrapped around and
        // a huge number of samples were tried to read. Strangely enough, this caused the
        // DecodeBuffer->chech_buffer_capacity() to crash while it was deleting the buffers
        // probably the reason for this problem lies in corruption of data?
        // This check at least tries to prevent this from happening.
        // N.B.: problem was observed when changing audio device params
        if (toRead < 0) {
            toRead = 0;
        }
        bufferUsed += m_reader->read(m_resampleDecodeBuffer, toRead);
		
		if (m_overflowUsed) {
            m_resampleDecodeBuffer.set_destination_buffer_read_offset(0);
        }
		//printf("Resampler: Read %lu of %lu (%lu)\n", bufferUsed, fileCnt + OVERFLOW_SIZE - m_overflowUsed, m_reader->get_length());
	}
	
	// Don't read extra frames next time.
	m_readExtraFrames = 0;
	
	if (m_reader->eof()) {
        m_privateSRC->srcData.end_of_input = 1;
	}
	
	nframes_t framesToConvert = frameCount;
    if (frameCount > m_fileFrames - m_readPos) {
        framesToConvert = m_fileFrames - m_readPos;
	}
	
    for (uint chan = 0; chan < m_channels; chan++) {
		// Set up sample rate converter struct for s.r.c. processing
        m_privateSRC->srcData.data_in = m_resampleDecodeBuffer.get_destination_buffer(chan).get_data(bufferUsed);
        m_privateSRC->srcData.input_frames = bufferUsed;
        m_privateSRC->srcData.data_out = buffer.get_destination_buffer(chan).get_data(framesToConvert);
        m_privateSRC->srcData.output_frames = framesToConvert;
        m_privateSRC->srcData.src_ratio = double(m_outputSampleRate) / m_fileSampleRate;
        src_set_ratio(m_privateSRC->srcStates[chan], m_privateSRC->srcData.src_ratio);
		
        if (src_process(m_privateSRC->srcStates[chan], &m_privateSRC->srcData)) {
			PERROR("Resampler: src_process() error!");
			return 0;
		}
        framesRead = nframes_t(m_privateSRC->srcData.output_frames_gen);
	}
	
    m_overflowUsed = bufferUsed - m_privateSRC->srcData.input_frames_used;
    Q_ASSERT(m_overflowUsed <= OVERFLOW_SIZE);
	if (m_overflowUsed < 0) {
		m_overflowUsed = 0;
    }
	if (m_overflowUsed) {
		// If there was overflow, save it for the next read.
        m_resampleDecodeBuffer.set_destination_buffer_read_offset(m_privateSRC->srcData.input_frames_used);
        for (uint chan = 0; chan < m_channels; chan++) {
            TAudioBuffer::copy_data(*m_overflowBuffers.at(chan), m_resampleDecodeBuffer.get_destination_buffer(chan), m_overflowUsed);
		}
        m_resampleDecodeBuffer.set_destination_buffer_read_offset(0);
	}
	
	// Pad end of file with 0s if necessary
	if (framesRead == 0 && m_readPos < get_nframes()) {
        int padLength = int(get_nframes() - m_readPos);
        printf("Resampler: padding: %d\n", padLength);
        buffer.set_destination_buffer_read_offset(framesRead);
        for (uint chan = 0; chan < m_channels; chan++) {
            buffer.get_destination_buffer(chan).silence_data(padLength);
		}
        buffer.set_destination_buffer_read_offset(0);
        framesRead += nframes_t(padLength);
	}
	
	// Truncate so we don't return too many samples
	if (m_readPos + framesRead > get_nframes()) {
		printf("Resampler: truncating: %d\n", framesRead - (get_nframes() - m_readPos));
		framesRead = get_nframes() - m_readPos;
	}
	
// 	printf("framesRead: %lu of %lu (overflow: %lu) (at: %lu of %lu)\n", framesRead, frameCount, m_overflowUsed, m_readPos /*+ framesRead*/, get_nframes());
	
	return framesRead;
}


nframes_t ResampleAudioReader::resampled_to_file_frame(nframes_t frame)
{
    return TTimeRef::to_frame(TTimeRef(frame, m_outputSampleRate), m_fileSampleRate);
}


nframes_t ResampleAudioReader::file_to_resampled_frame(nframes_t frame)
{    
    return TTimeRef::to_frame(TTimeRef(frame, m_fileSampleRate), m_outputSampleRate);
}

void ResampleAudioReader::create_overflow_buffers()
{
    for (uint chan=0; chan < m_channels; ++chan) {
        m_overflowBuffers.push_back(std::make_unique<TRealTimeAudioBuffer>(OVERFLOW_SIZE));
    }
}

int ResampleAudioReader::get_default_resample_quality()
{
    return SRC_SINC_FASTEST;
}

QString ResampleAudioReader::get_convertor_type_name(int convertorType)
{
    return QString(src_get_name(convertorType));
}

QString ResampleAudioReader::get_convertor_type_description(int convertorType)
{
    return QString(src_get_description(convertorType));
}

