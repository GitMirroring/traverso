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
#include "TAudioBuffer.h"
#include "TLog.h"

RELAYTOOL_WAVPACK;

WPAudioReader::WPAudioReader(const QString& filename)
    : TAudioDecoder(filename)
{
    char error[80];

    m_wp = WavpackOpenFileInput(m_fileName.toUtf8().data(), error, OPEN_NORMALIZE | OPEN_WVC, 0);

    if (m_wp == nullptr) {
        m_openError = TAudioFileError{TAudioFileError::Code::CannotOpen, QString::fromUtf8(error)};
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


TAudioFileStatus WPAudioReader::seek(nframes_t frame)
{
    Q_ASSERT(frame < m_frames);
    if (!m_wp) {
        return seek_failed(frame);
    }

    // WavPack's name: a sample of every channel, a frame here
    if (!WavpackSeekSample(m_wp, frame)) {
        return seek_failed(frame, QString::fromUtf8(WavpackGetErrorMessage(m_wp)));
    }

    return seek_landed(frame);
}


TAudioFileResult WPAudioReader::read(TAudioBuffer& fileIOBuffer, nframes_t count)
{
    if (count == 0) {
        return {};
    }
    fileIOBuffer.check_capacity(count, m_channels);

    nframes_t readFrames = 0;
    if (m_wp && !at_end() && !m_error) {
        // WavPack unpacks into int32_t, the buffers are float: scratch per
        // thread, used within this read only, grown to the largest read
        static thread_local std::vector<int32_t> unpackBuffer;
        if (unpackBuffer.size() < size_t(count) * m_channels) {
            unpackBuffer.resize(size_t(count) * m_channels);
        }
        readFrames = WavpackUnpackSamples(m_wp, unpackBuffer.data(), count);
        unpack(fileIOBuffer, unpackBuffer.data(), readFrames);
    }

    // WavPack's last message, when a short read is its error
    const QString detail = m_wp && readFrames < count && WavpackGetNumErrors(m_wp) > 0 ? QString::fromUtf8(WavpackGetErrorMessage(m_wp)) : QString();
    return read_done(fileIOBuffer, readFrames, count, detail);
}

void WPAudioReader::unpack(TAudioBuffer& fileIOBuffer, const int32_t* unpackBuffer, nframes_t readFrames)
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
            TChannelBuffer &dest = fileIOBuffer.get_channel_buffer(0);
            std::memcpy(dest.get_data(readFrames), floatSrc, readFrames * sizeof(float));
            break;
        }
        case 2:
        {
            TChannelBuffer &left = fileIOBuffer.get_channel_buffer(0);
            TChannelBuffer &right = fileIOBuffer.get_channel_buffer(1);

            for (nframes_t f = 0; f < readFrames; f++) {
                left[f]  = floatSrc[f * 2];
                right[f] = floatSrc[f * 2 + 1];
            }
            break;
        }
        default:
        {
            for (uint channel = 0; channel < m_channels; channel++) {
                TChannelBuffer &destBuffer = fileIOBuffer.get_channel_buffer(channel);
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
            TChannelBuffer &destination = fileIOBuffer.get_channel_buffer(0);
            for (nframes_t frame = 0; frame < readFrames; frame++) {
                destination[frame] = static_cast<float>(unpackBuffer[frame]) / divider;
            }
            break;
        }
        case 2:
        {
            TChannelBuffer &left = fileIOBuffer.get_channel_buffer(0);
            TChannelBuffer &right = fileIOBuffer.get_channel_buffer(1);

            for (nframes_t frame = 0; frame < readFrames; frame++) {
                uint index = frame * 2;
                left[frame]  = static_cast<float>(unpackBuffer[index]) / divider;
                right[frame] = static_cast<float>(unpackBuffer[index + 1]) / divider;
            }
            break;
        }
        default:
            for (uint channel = 0; channel < m_channels; channel++) {
                TChannelBuffer &destBuffer = fileIOBuffer.get_channel_buffer(channel);
                for (nframes_t frame = 0; frame < readFrames; frame++) {
                    destBuffer[frame] = static_cast<float>(unpackBuffer[frame * m_channels + channel]) / divider;
                }
            }
        }
    }
}

