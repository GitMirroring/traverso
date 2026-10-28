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

#include <cerrno>
#include <cstring>
#include <tuple>
#include <vector>

WPAudioWriter::WPAudioWriter(const TAudioEncodeSettings& settings)
    : TAudioEncoder(settings)
{
}


WPAudioWriter::~WPAudioWriter()
{
    // TAudioWriter closes it and reports how that went; this is the last
    // resort for an encoder destroyed with its file open
    std::ignore = WPAudioWriter::close();
    delete [] m_firstBlock;
}


TAudioFileStatus WPAudioWriter::open(const QString& fileName)
{
	std::ignore = close();
	m_writeError.clear();
	m_file = fopen(fileName.toUtf8().data(), "wb");
	if (!m_file) {
		return std::unexpected(TAudioFileError{TAudioFileError::Code::CannotOpen, QString::fromUtf8(std::strerror(errno))});
	}
	
	m_wp = WavpackOpenFileOutput(WPAudioWriter::write_block, (void *)this, NULL);
	if (!m_wp) {
		fclose(m_file);
		m_file = nullptr;
		return std::unexpected(TAudioFileError{TAudioFileError::Code::CannotOpen, QStringLiteral("WavPack cannot start an encoder")});
	}

	memset (&m_config, 0, sizeof(m_config));
    const int bitDepth = m_settings.bit_depth();
    m_config.bytes_per_sample = bitDepth/8;
    m_config.bits_per_sample = bitDepth;
    if (m_settings.dataFormat == TraversoDAW::DataFormat::FLOAT) {
        m_config.float_norm_exp = 127; // config->float_norm_exp,  select floating-point data (127 for +/-1.0)
    }
    // The speakers of the channels (the Microsoft channel mask): mono is
    // centre, stereo front left and right; more channels (the inputs of an
    // interface) are no speakers, so none
    m_config.channel_mask = m_settings.channelCount == 1 ? 4 : m_settings.channelCount == 2 ? 3 : 0;
    m_config.num_channels = m_settings.channelCount;
    m_config.sample_rate = m_settings.sampleRate;
	// The compression the user chose: fast, high (about 1.5 times slower than
	// fast, about 20% smaller), very high (about 2 times slower, about 25%
	// smaller); and without the correction data of floating point samples
	// (WVX: about 10% smaller, lossy below 25 bits of resolution)
	switch (m_settings.wavpackCompression) {
	case TAudioEncodeSettings::WavPackCompression::Fast:
		m_config.flags = CONFIG_FAST_FLAG;
		break;
	case TAudioEncodeSettings::WavPackCompression::VeryHigh:
		m_config.flags = CONFIG_HIGH_FLAG | CONFIG_VERY_HIGH_FLAG;
		break;
	case TAudioEncodeSettings::WavPackCompression::High:
	default:
		m_config.flags = CONFIG_HIGH_FLAG;
		break;
	}
	if (m_settings.wavpackSkipWvx) {
		m_config.flags |= CONFIG_SKIP_WVX;
	}
	
	WavpackSetConfiguration(m_wp, &m_config, -1);
	
	if (!WavpackPackInit(m_wp)) {
		const QString why = QString::fromUtf8(WavpackGetErrorMessage(m_wp));
		fclose(m_file);
		m_file = nullptr;
		WavpackCloseFile(m_wp);
		m_wp = nullptr;
		return std::unexpected(TAudioFileError{TAudioFileError::Code::CannotOpen, why});
	}
	
	m_firstBlock = 0;
	m_firstBlockSize = 0;
	
	return {};
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
			// The context stays: WavPack is inside it now, and close()
			// releases it and the file
			writer->m_writeError = QString::fromUtf8(std::strerror(errno));
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


TAudioFileResult WPAudioWriter::write(void* buffer, nframes_t frameCount)
{
    if (frameCount == 0) {
        return {};
    }
    if (!m_wp || !buffer) {
        return {0, TAudioFileError{TAudioFileError::Code::WriteFailed, QStringLiteral("no file open")}};
    }
    // Why packing failed: the file did not take a block, or WavPack's own
    // reason
    const auto failed = [this]() -> TAudioFileResult {
        return {0, TAudioFileError{TAudioFileError::Code::WriteFailed,
                                   m_writeError.isEmpty() ? QString::fromUtf8(WavpackGetErrorMessage(m_wp)) : m_writeError}};
    };

    uint channels = m_settings.channelCount;
    nframes_t totalSamples = frameCount * channels;
    TraversoDAW::DataFormat dataFormat = m_settings.dataFormat;

    // =========================================================================
    // CASE 1: Fixed Integer Bit-Depths (8-bit, 16-bit, 24-bit PCM inputs)
    // WavpackPackSamples requires all samples to be aligned inside an int32_t array.
    // =========================================================================
    if (dataFormat == TraversoDAW::DataFormat::PCM_S8 ||
        dataFormat == TraversoDAW::DataFormat::PCM_16 ||
        dataFormat == TraversoDAW::DataFormat::PCM_24)
    {
        // The samples widened to int32_t: scratch per thread, used within
        // this call only (WavPack copies them into its own blocks), grown to
        // the largest write on the thread
        static thread_local std::vector<int32_t> widened;
        if (widened.size() < totalSamples) {
            widened.resize(totalSamples);
        }
        int32_t* samples = widened.data();

        // Perform type-safe bit-extension up to 32-bit integer scale boundaries
        for (nframes_t s = 0; s < totalSamples; s++) {
            switch (dataFormat) {
            case TraversoDAW::DataFormat::PCM_S8:
                // Unsigned, around 128, as gdither writes 8 bits (GDither8bit)
                samples[s] = static_cast<int32_t>(static_cast<uint8_t*>(buffer)[s]) - 128;
                break;

            case TraversoDAW::DataFormat::PCM_16:
                // FIXED: Compile-time constant enum comparison resolves the legacy 'case 16' bug
                samples[s] = static_cast<int32_t>(static_cast<int16_t*>(buffer)[s]);
                break;

            case TraversoDAW::DataFormat::PCM_24:
                // In the upper 24 bits of a 32 bit word, as gdither writes it
                // (GDither32bit); WavPack takes it in the lower 24
                samples[s] = static_cast<int32_t*>(buffer)[s] >> 8;
                break;

            default:
                samples[s] = 0;
                break;
            }
        }

        // Push the aligned int32_t block to the WavPack compression engine
        if (WavpackPackSamples(m_wp, samples, frameCount) == false) {
            return failed();
        }
        return {frameCount, std::nullopt};
    }

    // =========================================================================
    // CASE 2: Native 32-bit Inputs (PCM_32 Integers or Standard FLOAT streams)
    // Audio data already occupies 4-byte boundaries, pass directly to compression
    // =========================================================================
    if (WavpackPackSamples(m_wp, static_cast<int32_t*>(buffer), frameCount) == false) {
        return failed();
    }

    return {frameCount, std::nullopt};
}


TAudioFileStatus WPAudioWriter::close()
{
	if (!m_wp) {
		return {};
	}
	std::optional<TAudioFileError> error;
	
	if (WavpackFlushSamples(m_wp) == false) {
		error = TAudioFileError{TAudioFileError::Code::WriteFailed,
		                        m_writeError.isEmpty() ? QString::fromUtf8(WavpackGetErrorMessage(m_wp)) : m_writeError};
	}
	if (rewrite_first_block() == false && !error) {
		error = TAudioFileError{TAudioFileError::Code::WriteFailed,
		                        m_writeError.isEmpty() ? QStringLiteral("the first block could not be written again") : m_writeError};
	}
	
	WavpackCloseFile(m_wp);
	m_wp = 0;
	
	// stdio writes its last bytes here: a full disk shows now
	if (fclose(m_file) != 0 && !error) {
		error = TAudioFileError{TAudioFileError::Code::WriteFailed, QString::fromUtf8(std::strerror(errno))};
	}
	m_file = 0;


	if (m_firstBlock) {
		delete [] m_firstBlock;
		m_firstBlock = 0;
		m_firstBlockSize = 0;
	}
	
	if (error) {
		return std::unexpected(std::move(*error));
	}
	return {};
}

