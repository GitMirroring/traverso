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

#include "SFAudioWriter.h"
#include "TExportSpecification.h"
#include "Utils.h"

#include <QString>



#include "Debugger.h"


SFAudioWriter::SFAudioWriter(TExportSpecification *spec)
    : AbstractAudioWriter(spec)
{
	m_sf = 0;
}


SFAudioWriter::~SFAudioWriter()
{
	if (m_sf) {
        SFAudioWriter::close_private();
	}
}




bool SFAudioWriter::open_private()
{
    char errbuf[256];
    std::memset(&m_sfinfo, 0, sizeof(m_sfinfo));

    // =========================================================================
    // TRANSLATION STAGE: Map TraversoDAW compile-time enums to libsndfile bitmasks
    // =========================================================================
    int sf_file_format = 0;
    switch (m_exportSpecification->get_file_format()) {
    case TraversoDAW::FileFormat::WAV:   sf_file_format = SF_FORMAT_WAV; break;
    case TraversoDAW::FileFormat::AIFF:  sf_file_format = SF_FORMAT_AIFF; break;
    case TraversoDAW::FileFormat::W64:   sf_file_format = SF_FORMAT_W64; break;
    case TraversoDAW::FileFormat::FLAC:  sf_file_format = SF_FORMAT_FLAC; break;
    case TraversoDAW::FileFormat::OGG:   sf_file_format = SF_FORMAT_OGG; break;
    case TraversoDAW::FileFormat::MP3:   sf_file_format = SF_FORMAT_MPEG; break;
    default:                             sf_file_format = SF_FORMAT_WAV; break;
    }

    int sf_data_format = 0;
    switch (m_exportSpecification->get_data_format()) {
    case TraversoDAW::DataFormat::PCM_S8: sf_data_format = SF_FORMAT_PCM_S8; break;
    case TraversoDAW::DataFormat::PCM_16: sf_data_format = SF_FORMAT_PCM_16; break;
    case TraversoDAW::DataFormat::PCM_24: sf_data_format = SF_FORMAT_PCM_24; break;
    case TraversoDAW::DataFormat::PCM_32: sf_data_format = SF_FORMAT_PCM_32; break;
    case TraversoDAW::DataFormat::FLOAT:
    default:                              sf_data_format = SF_FORMAT_FLOAT; break;
    }

    // Combine format headers and bit-depth quantization flags safely
    m_sfinfo.format = sf_file_format | sf_data_format;

    m_sfinfo.frames = 0;

    m_sfinfo.samplerate = m_exportSpecification->get_sample_rate();
    m_sfinfo.channels = m_exportSpecification->get_channel_count();

    m_file.setFileName(m_fileName);
    if (!m_file.open(QIODevice::WriteOnly)) {
        qWarning("SFAudioWriter::open_private: Could not create file (%s)", QS_C(m_fileName));
        return false;
    }

    m_sf = sf_open_fd(m_file.handle(), SFM_WRITE, &m_sfinfo, false);

    // Apply specific variable bitrate encoding options if OGG or MP3 formats are selected
    if (m_sf && (m_exportSpecification->get_file_format() == TraversoDAW::FileFormat::OGG ||
                 m_exportSpecification->get_file_format() == TraversoDAW::FileFormat::MP3))
    {
        double quality = 5.0; // Default medium VBR execution boundaries
        sf_command(m_sf, SFC_SET_VBR_ENCODING_QUALITY, &quality, sizeof(double));
    }

    if (m_sf == nullptr) {
        sf_error_str(nullptr, errbuf, sizeof(errbuf) - 1);
        PWARN(QString("SFAudioWriter: Cannot open output file via descriptor \"%1\" (%2)").arg(m_fileName, errbuf).toLatin1().data());
        m_file.close();
        return false;
    }

    return true;
}



nframes_t SFAudioWriter::write_private(void* buffer, nframes_t frameCount)
{
    Q_ASSERT(m_exportSpecification);
    Q_ASSERT(m_sf);
    Q_ASSERT(buffer);

    int written = 0;
    char errbuf[256];
    uint channels = m_exportSpecification->get_channel_count();
    TraversoDAW::DataFormat dataFormat = m_exportSpecification->get_data_format();

    switch (dataFormat) {
    case TraversoDAW::DataFormat::PCM_S8:
        // 8-bit raw byte block transmission
        written = sf_write_raw(m_sf, buffer, frameCount * channels);
        // sf_write_raw returns total samples written, scale it back to frames for evaluation consistency
        written = (written > 0) ? (written / channels) : 0;
        break;

    case TraversoDAW::DataFormat::PCM_16:
        // 16-bit integer short array frame block translation
        written = sf_writef_short(m_sf, static_cast<short*>(buffer), frameCount);
        break;

    case TraversoDAW::DataFormat::PCM_24:
    case TraversoDAW::DataFormat::PCM_32:
        // 24-bit and 32-bit integer array frame block translation
        written = sf_writef_int(m_sf, static_cast<int*>(buffer), frameCount);
        break;

    case TraversoDAW::DataFormat::FLOAT:
    default:
        // 32-bit standard floating point pass-through pipeline
        written = sf_writef_float(m_sf, static_cast<float*>(buffer), frameCount);
        break;
    }

    // Validate bounds to trap pipeline and device storage underruns
    if (written < 0 || static_cast<nframes_t>(written) != frameCount) {
        sf_error_str(m_sf, errbuf, sizeof(errbuf) - 1);
        PERROR(QString("SFAudioWriter: Export block write failed. libsndfile error: %1").arg(errbuf).toLatin1().data());
        return 0; // Return 0 instead of -1 to align with unsigned nframes_t error contracts safely
    }

    return static_cast<nframes_t>(written);
}



bool SFAudioWriter::close_private()
{
	bool success = (sf_close(m_sf) == 0);
	
	m_sf = nullptr;
	
	return success;
}

