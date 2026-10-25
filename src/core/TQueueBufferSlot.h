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


#pragma once

#include "TAudioBuffer.h"
#include "defines.h"
#include "TTimeRef.h"

class TQueueBufferSlot {
public:
    explicit TQueueBufferSlot(int slotNumber, uint channelCount, nframes_t bufferSize)
        : m_slotNumber(slotNumber), m_bufferSize(bufferSize)
    {
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

    void read_buffer(TAudioBuffer &dest, uint channel, nframes_t nframes) T_REALTIME_SAFE {
        Q_ASSERT(nframes <= m_bufferSize);
        Q_ASSERT(nframes > 0);
        // operator[]: at() throws
        Q_ASSERT(channel < m_buffers.size());
        TAudioBuffer::copy_data(dest, *m_buffers[channel], nframes);
    }

    // Where the slot's frames are: on the transport, in the file, and from
    // which slot position on they are the stream's (a clip after a seek
    // starts at offset). Once per slot, before write_channel()
    void update_location_and_offset(const TTimeRef& transportLocation, const TTimeRef& fileLocation, nframes_t offset = 0) T_REALTIME_SAFE {
        Q_ASSERT(offset < m_bufferSize);
        m_transportLocation = transportLocation;
        m_fileLocation = fileLocation;
        m_bufferWriteOffset = offset;
    }

    // count frames of source, from its frame sourceAt, into channel from the
    // slot's offset on. The positions before the offset are silence: a slot
    // comes back from the free queue with what it held before
    void write_channel(uint channel, const TAudioBuffer& source, nframes_t sourceAt, nframes_t count) T_REALTIME_SAFE {
        Q_ASSERT(m_bufferWriteOffset + count <= m_bufferSize);
        Q_ASSERT(count > 0);
        // operator[]: at() throws
        Q_ASSERT(channel < m_buffers.size());
        TAudioBuffer& buffer = *m_buffers[channel];
        buffer.silence(0, m_bufferWriteOffset);
        buffer.copy_from(m_bufferWriteOffset, source, sourceAt, count);
    }

    void set_file_location(const TTimeRef& fileLocation) {
        m_fileLocation = fileLocation;
    }

    void set_transport_location(const TTimeRef& transportLocation) {
        m_transportLocation = transportLocation;
    }

private:
    TTimeRef            m_fileLocation{};
    TTimeRef            m_transportLocation{};
    int                 m_slotNumber{-1};
    std::vector<std::unique_ptr<TRealTimeAudioBuffer>> m_buffers;
    nframes_t           m_bufferSize{0};
    nframes_t           m_bufferWriteOffset{0};
};

