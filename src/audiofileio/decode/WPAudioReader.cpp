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
#include "TFileIOBuffer.h"
#include "TLog.h"

RELAYTOOL_WAVPACK;

WPAudioReader::WPAudioReader(const QString& filename)
    : TAudioDecoder(filename)
{
    char error[80];

    m_wp = WavpackOpenFileInput(m_fileName.toUtf8().data(), error, OPEN_NORMALIZE | OPEN_WVC, 0);

    if (m_wp == nullptr) {
        TLOG_W("audiofile", "{}: WavPack cannot read it ({})", filename.toStdString(), error);
        return;
    }

    m_isFloat = ((WavpackGetMode(m_wp) & MODE_FLOAT) != 0);
    m_bitsPerSample = WavpackGetBitsPerSample(m_wp);
    m_bytesPerSample = WavpackGetBytesPerSample(m_wp);
    m_channels = WavpackGetReducedChannels(m_wp);
    m_frames = WavpackGetNumSamples(m_wp);
    m_rate = WavpackGetSampleRate(m_wp);
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

    WavpackContext *wp = WavpackOpenFileInput(filename.toUtf8().data(), error, OPEN_NORMALIZE | OPEN_WVC, 0);

    if (wp == nullptr) {
        return false;
    }

    WavpackCloseFile(wp);

    return true;
}

void WPAudioReader::print_libwavpack_version()
{
    TLOG_D("audiofile", "WavPack version {}", WavpackGetLibraryVersionString());
}


bool WPAudioReader::seek(nframes_t frame)
{
    if (!m_wp || frame >= m_frames) {
        return false;
    }

    // WavPack's name: a sample of every channel, a frame here
    if (!WavpackSeekSample(m_wp, frame)) {
        TLOG_W("audiofile", "{}: WavPack cannot seek to frame {} ({})", m_fileName.toStdString(), frame, WavpackGetErrorMessage(m_wp));
        return false;
    }

    m_frame = frame;
    return true;
}


nframes_t WPAudioReader::read(TFileIOBuffer& fileIOBuffer, nframes_t count)
{
    if (count == 0) {
        return 0;
    }
    fileIOBuffer.check_capacity(count, m_channels);

    nframes_t readFrames = 0;
    if (m_wp && !at_end()) {
        // WavPack unpacks into int32_t, the buffers are float
        std::vector<int32_t> unpackBuffer(size_t(count) * m_channels);
        readFrames = WavpackUnpackSamples(m_wp, unpackBuffer.data(), count);
        unpack(fileIOBuffer, unpackBuffer.data(), readFrames);
    }

    // What the file did not give: zeros (the contract of TAudioDecoder::read())
    fileIOBuffer.silence(readFrames, count - readFrames, m_channels);
    m_frame += readFrames;
    return readFrames;
}

void WPAudioReader::unpack(TFileIOBuffer& fileIOBuffer, const int32_t* unpackBuffer, nframes_t readFrames)
{
    if (readFrames == 0) {
        return;
    }


    // calculate divider for normalization for float -1.0 .. 1.0
    const float divider = static_cast<float>((1ULL << (m_bytesPerSample * 8 - 1)));

    if (m_isFloat) {
        const float* floatSrc = reinterpret_cast<const float*>(unpackBuffer);

        switch (m_channels) {
        case 1:
        {
            TAudioBuffer &dest = fileIOBuffer.get_channel_buffer(0);
            std::memcpy(dest.get_data(readFrames), floatSrc, readFrames * sizeof(float));
            break;
        }
        case 2:
        {
            TAudioBuffer &left = fileIOBuffer.get_channel_buffer(0);
            TAudioBuffer &right = fileIOBuffer.get_channel_buffer(1);

            for (nframes_t f = 0; f < readFrames; f++) {
                left[f]  = floatSrc[f * 2];
                right[f] = floatSrc[f * 2 + 1];
            }
            break;
        }
        default:
        {
            for (uint channel = 0; channel < m_channels; channel++) {
                TAudioBuffer &destBuffer = fileIOBuffer.get_channel_buffer(channel);
                for (nframes_t frame = 0; frame < readFrames; frame++) {
                    destBuffer[frame] = floatSrc[frame * m_channels + channel];
                }
            }
        }
        }
    }
    else {
        switch (m_channels) {
        case 1:
        {
            TAudioBuffer &destination = fileIOBuffer.get_channel_buffer(0);
            for (nframes_t frame = 0; frame < readFrames; frame++) {
                destination[frame] = static_cast<float>(unpackBuffer[frame]) / divider;
            }
            break;
        }
        case 2:
        {
            TAudioBuffer &left = fileIOBuffer.get_channel_buffer(0);
            TAudioBuffer &right = fileIOBuffer.get_channel_buffer(1);

            for (nframes_t frame = 0; frame < readFrames; frame++) {
                uint index = frame * 2;
                left[frame]  = static_cast<float>(unpackBuffer[index]) / divider;
                right[frame] = static_cast<float>(unpackBuffer[index + 1]) / divider;
            }
            break;
        }
        default:
            for (uint channel = 0; channel < m_channels; channel++) {
                TAudioBuffer &destBuffer = fileIOBuffer.get_channel_buffer(channel);
                for (nframes_t frame = 0; frame < readFrames; frame++) {
                    destBuffer[frame] = static_cast<float>(unpackBuffer[frame * m_channels + channel]) / divider;
                }
            }
        }
    }
}

