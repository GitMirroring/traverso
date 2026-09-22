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

void WPAudioReader::print_libwavpack_version()
{
    printf("Wavpack version %s\n", WavpackGetLibraryVersionString());
}


bool WPAudioReader::seek_private(nframes_t frameToSeekTo)
{
    Q_ASSERT(m_wp);


    if (frameToSeekTo >= m_fileFrames) {
        return false;
    }

    if (!WavpackSeekSample(m_wp, frameToSeekTo)) {
        printf("WPAudioRead::seek_private: Could not seek to frame %d, reason %s\n", frameToSeekTo, WavpackGetErrorMessage(m_wp));
        return false;
    }

    return true;
}


nframes_t WPAudioReader::read_private(TFileDecodeBuffer& fileDecodeBuffer, nframes_t frameCount)
{
    Q_ASSERT(m_wp);

    uint32_t totalSamples = frameCount * m_channels;
    // WavPack reads in int32_t, fileDecodeBuffer uses float so we need temporary buffer here
    std::vector<int32_t> unpackBuffer(totalSamples);

    nframes_t readFrames = WavpackUnpackSamples(m_wp, unpackBuffer.data(), frameCount);

    if (readFrames == 0) {
        return 0;
    }


    // calculate divider for normalization for float -1.0 .. 1.0
    const float divider = static_cast<float>((1ULL << (m_bytesPerSample * 8 - 1)));

    if (m_isFloat) {
        const float* floatSrc = reinterpret_cast<const float*>(unpackBuffer.data());

        switch (m_channels) {
        case 1:
        {
            TAudioBuffer &dest = fileDecodeBuffer.get_destination_buffer(0);
            std::memcpy(dest.get_data(readFrames), floatSrc, readFrames * sizeof(float));
            break;
        }
        case 2:
        {
            TAudioBuffer &left = fileDecodeBuffer.get_destination_buffer(0);
            TAudioBuffer &right = fileDecodeBuffer.get_destination_buffer(1);

            for (nframes_t f = 0; f < readFrames; f++) {
                left[f]  = floatSrc[f * 2];
                right[f] = floatSrc[f * 2 + 1];
            }
            break;
        }
        default:
        {
            for (uint channel = 0; channel < m_channels; channel++) {
                TAudioBuffer &destBuffer = fileDecodeBuffer.get_destination_buffer(channel);
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
            TAudioBuffer &destination = fileDecodeBuffer.get_destination_buffer(0);
            for (nframes_t frame = 0; frame < readFrames; frame++) {
                destination[frame] = static_cast<float>(unpackBuffer[frame]) / divider;
            }
            break;
        }
        case 2:
        {
            TAudioBuffer &left = fileDecodeBuffer.get_destination_buffer(0);
            TAudioBuffer &right = fileDecodeBuffer.get_destination_buffer(1);

            for (nframes_t frame = 0; frame < readFrames; frame++) {
                uint index = frame * 2;
                left[frame]  = static_cast<float>(unpackBuffer[index]) / divider;
                right[frame] = static_cast<float>(unpackBuffer[index + 1]) / divider;
            }
            break;
        }
        default:
            for (uint channel = 0; channel < m_channels; channel++) {
                TAudioBuffer &destBuffer = fileDecodeBuffer.get_destination_buffer(channel);
                for (nframes_t frame = 0; frame < readFrames; frame++) {
                    destBuffer[frame] = static_cast<float>(unpackBuffer[frame * m_channels + channel]) / divider;
                }
            }
        }
    }

    return readFrames;
}

