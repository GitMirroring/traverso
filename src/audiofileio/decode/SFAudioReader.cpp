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

#include "SFAudioReader.h"
#include <QString>

#include "TFileIOBuffer.h"
#include "TLog.h"


SFAudioReader::SFAudioReader(const QString& filename)
    : TAudioDecoder(filename)
{
    /* although libsndfile says we don't need to set this,
	valgrind and source code shows us that we do.
	Really? Look it up !
	*/
    memset (&m_sfinfo, 0, sizeof(m_sfinfo));

    m_file.setFileName(m_fileName);

    if (!m_file.open(QIODevice::ReadOnly)) {
        TLOG_W("audiofile", "{}: cannot open it ({})", m_fileName.toStdString(), m_file.errorString().toStdString());
        return;
    }

    if ((m_sf = sf_open_fd (m_file.handle(), SFM_READ, &m_sfinfo, false)) == nullptr) {
        TLOG_W("audiofile", "{}: libsndfile cannot read it ({})", m_fileName.toStdString(), sf_strerror(nullptr));
        return;
    }

    m_channels = m_sfinfo.channels;
    m_frames = m_sfinfo.frames;
    m_rate = m_sfinfo.samplerate;
}


SFAudioReader::~SFAudioReader()
{
    if (m_sf) {
        if (sf_close(m_sf)) {
            TLOG_W("audiofile", "{}: closing it failed", m_fileName.toStdString());
        }
    }
}


int SFAudioReader::major_format(const QString& filename)
{
    SF_INFO infos;

    /* although libsndfile says we don't need to set this,
	valgrind and source code shows us that we do.
	*/
    memset (&infos, 0, sizeof(infos));

    QFile file(filename);

    // A probe: a file that does not open is one libsndfile cannot read
    if (!file.open(QIODevice::ReadOnly)) {
        return 0;
    }

    SNDFILE* sndfile = sf_open_fd(file.handle(), SFM_READ, &infos, false);

    //is it supported by libsndfile?
    if (!sndfile) {
        return 0;
    }

    sf_close(sndfile);

    return infos.format & SF_FORMAT_TYPEMASK;
}


bool SFAudioReader::seek(nframes_t frame)
{
    Q_ASSERT(frame < m_frames);
    if (!m_sf) {
        return seek_failed();
    }

    // libsndfile's Ogg seek is exact from the start of the file only: from
    // another position some seeks land a few hundred frames off (libsndfile
    // 1.2.2, test_audio_decoders). Back to the start first; a seek there is
    // cheap, the seek from it is a bisection of the file either way.
    if ((m_sfinfo.format & SF_FORMAT_TYPEMASK) == SF_FORMAT_OGG && sf_seek(m_sf, 0, SEEK_SET) < 0) {
        return seek_failed();
    }

    if (sf_seek (m_sf, (off_t) frame, SEEK_SET) < 0) {
        return seek_failed();
    }

    return seek_landed(frame);
}


nframes_t SFAudioReader::read(TFileIOBuffer& fileIOBuffer, nframes_t count)
{
    if (count == 0) {
        return 0;
    }
    fileIOBuffer.check_capacity(count, m_channels);

    TAudioBuffer &readBuffer = fileIOBuffer.get_file_io_interleaved_buffer();

    const nframes_t readFrames = (m_sf && !at_end() && !m_error) ? nframes_t(sf_readf_float(m_sf, readBuffer.get_data(count * m_channels), count)) : 0;

    // De-interlace
    switch (m_channels) {
    case 0:
        return 0;
    case 1:
    {
        TAudioBuffer::copy_data(fileIOBuffer.get_channel_buffer(0), readBuffer, readFrames);
        break;
    }
    case 2:
    {
        audio_sample_t* left = fileIOBuffer.get_channel_buffer(0).get_data(readFrames);
        audio_sample_t* right = fileIOBuffer.get_channel_buffer(1).get_data(readFrames);
        for (nframes_t frame = 0; frame < readFrames; frame++) {
            int index = frame*2;
            left[frame] = readBuffer[index];
            right[frame] = readBuffer[index + 1];
        }
        break;
    }
    default:
    {
        for (uint channel = 0; channel < m_channels; channel++) {
            TAudioBuffer &destBuffer = fileIOBuffer.get_channel_buffer(channel);
            for (nframes_t frame = 0; frame < readFrames; frame++) {
                destBuffer[frame] = readBuffer[frame * m_channels + channel];
            }
        }
    }
    }

    return read_done(fileIOBuffer, readFrames, count);
}

