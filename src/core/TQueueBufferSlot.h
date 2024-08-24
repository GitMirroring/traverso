/*
Copyright (C) 2024 Remon Sijrier

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


#ifndef TQUEUEBUFFERSLOT_H
#define TQUEUEBUFFERSLOT_H

#include "TAudioBuffer.h"
#include "defines.h"
#include "TTimeRef.h"

class TQueueBufferSlot {
public:
    explicit TQueueBufferSlot(int slotNumber, uint channelCount, nframes_t bufferSize) {
        m_fileLocation = TTimeRef();
        m_transportLocation = TTimeRef();
        m_slotNumber = slotNumber;
        m_bufferSize = bufferSize;
        m_bufferWriteOffset = 0;
        for (uint chan=0; chan < channelCount; ++chan) {
            m_buffers.push_back(std::make_unique<TRealTimeAudioBuffer>(bufferSize));
        }
    }

    ~TQueueBufferSlot()
    {
    }

    int get_slot_number() const {return m_slotNumber;}
    inline nframes_t get_buffer_size() const {return m_bufferSize;}
    inline nframes_t get_read_nframes() const {return m_bufferSize - m_bufferWriteOffset;}
    inline TTimeRef get_file_location() const {return m_fileLocation;}
    inline TTimeRef get_transport_location() const {return m_transportLocation;}

    nframes_t get_buffer_write_offset() const {return m_bufferWriteOffset;}

    void read_buffer(TAudioBuffer &dest, uint channel, nframes_t nframes) {
        Q_ASSERT(nframes <= m_bufferSize);
        Q_ASSERT(nframes > 0);
        TAudioBuffer::copy_data(dest, *m_buffers.at(channel), nframes);
    }

    void write_buffer(const TTimeRef &transportLocation, const TTimeRef &fileLocation, audio_sample_t* source, uint channel, nframes_t nframes, nframes_t offset = 0) {
        Q_ASSERT(offset + nframes <= m_bufferSize);
        Q_ASSERT(nframes > 0);
        memcpy(m_buffers.at(channel)->get_data(nframes) + offset, source, nframes * sizeof(audio_sample_t));
        m_transportLocation = transportLocation;
        m_fileLocation = fileLocation;
        m_bufferWriteOffset = offset;
    }

    void set_file_location(const TTimeRef& fileLocation) {
        m_fileLocation = fileLocation;
    }

    void set_transport_location(const TTimeRef& transportLocation) {
        m_transportLocation = transportLocation;
    }

    void silence_buffers() {
        for(size_t i=0; i<m_buffers.size(); ++i) {
            m_buffers.at(i)->silence_data();
        }
    }

private:
    TTimeRef            m_fileLocation;
    TTimeRef            m_transportLocation;
    int                 m_slotNumber;
    std::vector<std::unique_ptr<TRealTimeAudioBuffer>> m_buffers;
    nframes_t           m_bufferSize;
    nframes_t           m_bufferWriteOffset;
};

#endif // TQUEUEBUFFERSLOT_H
