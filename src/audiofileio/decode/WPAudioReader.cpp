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

#include "WPAudioReader.h"
#include <QString>
#include "TFileDecodeBuffer.h"
#include "Utils.h"

RELAYTOOL_WAVPACK;



#include "Debugger.h"


WPAudioReader::WPAudioReader(const QString& filename)
    : AbstractAudioReader(filename)
{
    char error[80];

    m_wp = WavpackOpenFileInput(m_fileName.toUtf8().data(), error, OPEN_2CH_MAX | OPEN_NORMALIZE | OPEN_WVC, 0);

    if (m_wp == nullptr) {
        qWarning("Couldn't open soundfile (%s) %s", QS_C(filename), error);
        return;
    }

    m_isFloat = ((WavpackGetMode(m_wp) & MODE_FLOAT) != 0);
    m_bitsPerSample = WavpackGetBitsPerSample(m_wp);
    m_bytesPerSample = WavpackGetBytesPerSample(m_wp);
    m_channels = WavpackGetReducedChannels(m_wp);
    m_fileFrames = WavpackGetNumSamples(m_wp);
    m_fileSampleRate = WavpackGetSampleRate(m_wp);
    m_length = TTimeRef(m_fileFrames, m_fileSampleRate);
}


WPAudioReader::~WPAudioReader()
{
    if (m_wp) {
        WavpackCloseFile(m_wp);
    }
}


bool WPAudioReader::can_decode(const QString& filename)
{
    if (!libwavpack_is_present) {
        return false;
    }

    char error[80];

    WavpackContext *wp = WavpackOpenFileInput(filename.toUtf8().data(), error, OPEN_2CH_MAX | OPEN_NORMALIZE | OPEN_WVC, 0);

    if (wp == nullptr) {
        return false;
    }

    WavpackCloseFile(wp);

    return true;
}


bool WPAudioReader::seek_private(nframes_t start)
{
    Q_ASSERT(m_wp);


    if (start >= m_fileFrames) {
        return false;
    }

    if (!WavpackSeekSample(m_wp, start)) {
        //		PERROR("could not seek to frame %d within %s", start, QS_C(m_fileName));
        return false;
    }

    return true;
}


nframes_t WPAudioReader::read_private(TFileDecodeBuffer* buffer, nframes_t frameCount)
{
    Q_ASSERT(m_wp);

    // WavPack only reads into a int32_t buffer...
    int32_t* readbuffer = (int32_t*)buffer->get_read_buffer(frameCount * m_channels);

    nframes_t framesRead = WavpackUnpackSamples(m_wp, readbuffer, frameCount);

    const uint divider = ((uint)1<<(m_bytesPerSample * 8 - 1));

    // De-interlace
    if (m_isFloat) {
        switch (m_channels) {
        case 1:
        {
            memcpy(buffer->get_destination_buffer(0, framesRead), readbuffer, framesRead * sizeof(audio_sample_t));
            break;
        }
        case 2:
        {
            audio_sample_t* left = buffer->get_destination_buffer(0, framesRead);
            audio_sample_t* right = buffer->get_destination_buffer(1, framesRead);

            for (nframes_t f = 0; f < framesRead; f++) {
                uint index = f*2;
                left[f] = ((float*)readbuffer)[index];
                right[f] = ((float*)readbuffer)[index + 1];
            }
            break;
        }
        default:
        {
            for (nframes_t frame = 0; frame < framesRead; frame++) {
                for (uint channel = 0; channel < m_channels; channel++) {
                    buffer->get_destination_buffer(channel, framesRead)[frame] = ((float*)readbuffer)[frame * m_channels + channel];
                }
            }
        }
        }
    }
    else {
        switch (m_channels) {
        case 1:
        {
            audio_sample_t* destination = buffer->get_destination_buffer(0, framesRead);
            for (nframes_t frame = 0; frame < framesRead; frame++) {
                destination[frame] = (float)((float)readbuffer[frame]/ divider);
            }
            break;
        }
        case 2:
        {
            audio_sample_t* left = buffer->get_destination_buffer(0, framesRead);
            audio_sample_t* right = buffer->get_destination_buffer(1, framesRead);

            for (nframes_t frame = 0; frame < framesRead; frame++) {
                uint index = frame*2;
                left[frame] = (float)((float)readbuffer[index]/ divider);
                right[frame] = (float)((float)readbuffer[index + 1]/ divider);
            }
            break;
        }
        default:
            for (nframes_t frame = 0; frame < framesRead; frame++) {
                for (uint channel = 0; channel < m_channels; channel++) {
                    buffer->get_destination_buffer(channel, framesRead)[frame] = (float)((float)readbuffer[frame * m_channels + channel]/ divider);
                }
            }
        }
    }

    return framesRead;
}

