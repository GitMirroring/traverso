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

#include "AbstractAudioReader.h"
#include "TFileIOBuffer.h"

#include <QString>

#include "Debugger.h"


AbstractAudioReader::AbstractAudioReader(const QString& filename)
{
    m_fileName = filename;
    m_readPos = m_channels = m_fileFrames = 0;
    m_fileSampleRate = 0;
    m_length = TTimeRef();
}


AbstractAudioReader::~AbstractAudioReader()
    = default;


// Read cnt frames starting at start from the AudioReader, into dst
// uses seek() and read() from AudioReader subclass
nframes_t AbstractAudioReader::read_from(TFileIOBuffer &fileIOBuffer, nframes_t start, nframes_t count)
{
    // 	printf("read_from:: before_seek from %d, framepos is %d\n", start, m_readPos);

    if (!seek(start)) {
        return 0;
    }

    return read(fileIOBuffer, count);
}


uint AbstractAudioReader::get_num_channels()
{
    return m_channels;
}


uint AbstractAudioReader::get_file_rate()
{
    return m_fileSampleRate;
}


bool AbstractAudioReader::eof()
{
    return (m_readPos >= m_fileFrames);
}


nframes_t AbstractAudioReader::pos()
{
    return m_readPos;
}


bool AbstractAudioReader::seek(nframes_t start)
{
    PENTER3;
    if (m_readPos != start) {
        if (!seek_private(start)) {
            return false;
        }
        m_readPos = start;
    }

    return true;
}


nframes_t AbstractAudioReader::read(TFileIOBuffer &buffer, nframes_t count)
{
    if (count > 0 && m_readPos < m_fileFrames) {

        // Make sure the read buffer is big enough for this read
        buffer.check_capacity(count, m_channels);
        // and contains only zero's
        buffer.silence_buffers();

        // printf("read_from:: after_seek from %d, framepos is %d\n", start, m_readPos);
        nframes_t framesRead = read_private(buffer, count);

        m_readPos += framesRead;

        return framesRead;
    }

    return 0;
}
