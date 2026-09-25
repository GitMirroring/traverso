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

#include "WPAudioWriter.h"

#include <QString>
#include "TExportSpecification.h"
#include "Utils.h"
#include "Debugger.h"

WPAudioWriter::WPAudioWriter(TExportSpecification* spec)
    : AbstractAudioWriter(spec)
{
	m_wp = 0;
    m_firstBlock = 0;
	m_firstBlockSize = 0;
    m_tmp_buffer = 0;
	m_tmpBufferSize = 0;
    // Set some sensible default values
    // CONFIG_HIGH_FLAG (default) ~ 1.5 times slower then FAST, ~ 20% extra compression then FAST
    m_configFlags = 0;
    m_configFlags |= CONFIG_HIGH_FLAG;
    m_configFlags &= ~CONFIG_SKIP_WVX;
}


WPAudioWriter::~WPAudioWriter()
{
	if (m_wp) {
        WPAudioWriter::close_private();
	}
    if (m_firstBlock) {
        delete [] m_firstBlock;
    }
}

bool WPAudioWriter::set_format_attribute(const QString& key, const QString& value)
{
	if (key == "quality") {
		// Clear quality before or-ing in the new quality value
		m_configFlags &= ~(CONFIG_FAST_FLAG | CONFIG_HIGH_FLAG | CONFIG_VERY_HIGH_FLAG);
		
		if (value == "fast") {
			m_configFlags |= CONFIG_FAST_FLAG;
			return true;
		}
		else if (value == "high") {
			// CONFIG_HIGH_FLAG (default) ~ 1.5 times slower then FAST, ~ 20% extra compression then FAST
			m_configFlags |= CONFIG_HIGH_FLAG;
			return true;
		}
		else if (value == "very_high") {
			// CONFIG_VERY_HIGH_FLAG ~ 2 times slower then FAST, ~ 25 % extra compression then FAST
			m_configFlags |= CONFIG_HIGH_FLAG;
			m_configFlags |= CONFIG_VERY_HIGH_FLAG;
			return true;
		}
	}
	
	if (key == "skip_wvx") {
		if (value == "true") {
			// This option reduces the storage of some floating-point data files by up to about 10% by eliminating some 
			// information that has virtually no effect on the audio data. While this does technically make the compression 
			// lossy, it retains all the advantages of floating point data (>600 dB of dynamic range, no clipping, and 25 bits 
			// of resolution). This also affects large integer compression by limiting the resolution to 24 bits.
			m_configFlags |= CONFIG_SKIP_WVX;
			return true;
		}
		else if (value == "false") {
			m_configFlags &= ~CONFIG_SKIP_WVX;
			return true;
		}
	}
	
	return false;
}


bool WPAudioWriter::open_private()
{
	m_file = fopen(m_fileName.toUtf8().data(), "wb");
	if (!m_file) {
		qWarning("Couldn't open file %s.", QS_C(m_fileName));
		return false;
	}
	
	m_wp = WavpackOpenFileOutput(WPAudioWriter::write_block, (void *)this, NULL);
	if (!m_wp) {
		fclose(m_file);
		return false;
	}

	memset (&m_config, 0, sizeof(m_config));
    int bitDepth = m_exportSpecification->get_bit_depth();
    m_config.bytes_per_sample = bitDepth/8;
    m_config.bits_per_sample = bitDepth;
    if (m_exportSpecification->get_data_format() == TraversoDAW::DataFormat::FLOAT) {
        m_config.float_norm_exp = 127; // config->float_norm_exp,  select floating-point data (127 for +/-1.0)
    }
    m_config.channel_mask = (m_exportSpecification->get_channel_count() == 2) ? 3 : 4; // Microsoft standard (mono = 4, stereo = 3)
    m_config.num_channels = m_exportSpecification->get_channel_count();
    m_config.sample_rate = m_exportSpecification->get_sample_rate();
	m_config.flags = m_configFlags;
	
	WavpackSetConfiguration(m_wp, &m_config, -1);
	
	if (!WavpackPackInit(m_wp)) {
		fclose(m_file);
		WavpackCloseFile(m_wp);
		m_wp = 0;
		return false;
	}
	
	m_firstBlock = 0;
	m_firstBlockSize = 0;
	
	return true;
}


int WPAudioWriter::write_to_file(void *lpBuffer, uint32_t nNumberOfBytesToWrite, uint32_t *lpNumberOfBytesWritten)
{
	uint32_t bcount;
	
	*lpNumberOfBytesWritten = 0;
	
	while (nNumberOfBytesToWrite) {
		bcount = fwrite((uchar *) lpBuffer + *lpNumberOfBytesWritten, 1, nNumberOfBytesToWrite, m_file);
	
		if (bcount) {
			*lpNumberOfBytesWritten += bcount;
			nNumberOfBytesToWrite -= bcount;
		}
		else {
			break;
		}
	}
	int err = ferror(m_file);
	return !err;
}


int WPAudioWriter::write_block(void *id, void *data, int32_t length)
{
	WPAudioWriter* writer = (WPAudioWriter*) id;
	uint32_t bcount;
	
	if (writer && writer->m_file && data && length) {
        if (writer->m_firstBlock == 0) {
            writer->m_firstBlock = new char[length];
            memcpy(writer->m_firstBlock, data, length);
			writer->m_firstBlockSize = length;
		}
		if (!writer->write_to_file(data, (uint32_t)length, (uint32_t*)&bcount) || bcount != (uint32_t)length) {
			fclose(writer->m_file);
			writer->m_wp = 0;
			return false;
		}
	}

	return true;
}


bool WPAudioWriter::rewrite_first_block()
{
	if (!m_firstBlock || !m_file || !m_wp) {
		return false;
	}
    WavpackUpdateNumSamples (m_wp, m_firstBlock);
	if (fseek(m_file, 0, SEEK_SET) != 0) {
		return false;
	}
    if (!write_block(this, m_firstBlock, m_firstBlockSize)) {
		return false;
	}
	
	return true;
}


nframes_t WPAudioWriter::write_private(void* buffer, nframes_t frameCount)
{
    Q_ASSERT(m_exportSpecification);
    Q_ASSERT(buffer);

    uint channels = m_exportSpecification->get_channel_count();
    nframes_t totalSamples = frameCount * channels;
    TraversoDAW::DataFormat dataFormat = m_exportSpecification->get_data_format();

    // =========================================================================
    // CASE 1: Fixed Integer Bit-Depths (8-bit, 16-bit, 24-bit PCM inputs)
    // WavpackPackSamples requires all samples to be aligned inside an int32_t array.
    // =========================================================================
    if (dataFormat == TraversoDAW::DataFormat::PCM_S8 ||
        dataFormat == TraversoDAW::DataFormat::PCM_16 ||
        dataFormat == TraversoDAW::DataFormat::PCM_24)
    {
        // Dynamically resize our safe, pre-allocated internal integer scratch pad
        if (totalSamples > m_tmpBufferSize) {
            if (m_tmp_buffer) {
                delete [] m_tmp_buffer;
            }
            m_tmp_buffer = new int32_t[totalSamples];
            m_tmpBufferSize = totalSamples;
        }

        // Perform type-safe bit-extension up to 32-bit integer scale boundaries
        for (nframes_t s = 0; s < totalSamples; s++) {
            switch (dataFormat) {
            case TraversoDAW::DataFormat::PCM_S8:
                m_tmp_buffer[s] = static_cast<int32_t>(static_cast<int8_t*>(buffer)[s]);
                break;

            case TraversoDAW::DataFormat::PCM_16:
                // FIXED: Compile-time constant enum comparison resolves the legacy 'case 16' bug
                m_tmp_buffer[s] = static_cast<int32_t>(static_cast<int16_t*>(buffer)[s]);
                break;

            case TraversoDAW::DataFormat::PCM_24:
            {
                // Safely extract 3-byte packed PCM24 samples into standard 32-bit containers
                uint8_t* pcm24Ptr = static_cast<uint8_t*>(buffer) + (3 * s);
                int32_t val = (pcm24Ptr[0] << 8) | (pcm24Ptr[1] << 16) | (pcm24Ptr[2] << 24);
                m_tmp_buffer[s] = val >> 8; // Preserve original sign bit extension
            }
            break;

            default:
                m_tmp_buffer[s] = 0;
                break;
            }
        }

        // Push the aligned int32_t block to the WavPack compression engine
        if (WavpackPackSamples(m_wp, m_tmp_buffer, frameCount) == false) {
            PERROR("WPAudioWriter: WavpackPackSamples failed encoding integer block");
            return 0;
        }
        return frameCount;
    }

    // =========================================================================
    // CASE 2: Native 32-bit Inputs (PCM_32 Integers or Standard FLOAT streams)
    // Audio data already occupies 4-byte boundaries, pass directly to compression
    // =========================================================================
    if (WavpackPackSamples(m_wp, static_cast<int32_t*>(buffer), frameCount) == false) {
        PERROR("WPAudioWriter: WavpackPackSamples failed encoding raw 32-bit block");
        return 0;
    }

    return frameCount;
}


bool WPAudioWriter::close_private()
{
	bool success = true;
	
	if (WavpackFlushSamples(m_wp) == false) {
		success = false;
	}
	if (rewrite_first_block() == false) {
		success = false;
	}
	
	WavpackCloseFile(m_wp);
	m_wp = 0;
	
	fclose(m_file);
	m_file = 0;

    if (m_tmp_buffer) {
        delete [] m_tmp_buffer;
        m_tmp_buffer = 0;
	}
	m_tmpBufferSize = 0;

	if (m_firstBlock) {
		delete [] m_firstBlock;
		m_firstBlock = 0;
		m_firstBlockSize = 0;
	}
	
	return success;
}

