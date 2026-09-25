/*
Copyright (C) 2024-2026 Remon Sijrier

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
#include <memory>
#include <vector>

class TFileIOBuffer {

public:
    TFileIOBuffer()
        : m_interleavedBuffer(1)
        , m_channelBufferSize(1)
        , m_interleavedBufferSize(1)
    {
        // Default initialize to stereo footprint capacity
        for (uint chan = 0; chan < 2; ++chan) {
            m_channelBuffers.push_back(std::make_unique<TAudioBuffer>(1));
        }
    }

    ~TFileIOBuffer() {
    }

    // Accesses a single, standalone de-interleaved mono channel stream
    TAudioBuffer& get_channel_buffer(uint channel) {
        return *m_channelBuffers.at(channel);
    }

    // Accesses the contiguous interleaved floating point data stream array
    TAudioBuffer& get_file_io_interleaved_buffer() {
        return m_interleavedBuffer;
    }

    // Accesses the unmanaged raw byte cache block used for storing packed PCM integer bit-depths
    void* get_packed_byte_buffer() {
        return static_cast<void*>(m_packedByteBuffer.data());
    }

    // =========================================================================
    // Common Management API (Used by both Read and Write paths)
    // =========================================================================
    void silence_buffers() {
        m_interleavedBuffer.silence_data();
        for (const auto &buffer : m_channelBuffers) {
            buffer->silence_data();
        }
    }

    // Dynamically secures memory capacity across the float data containers
    void check_capacity(uint size, uint channels)
    {
        Q_ASSERT(size > 0);

        if (size <= m_channelBufferSize) {
            return;
        }

        uint maxChannels = m_channelBuffers.size();
        Q_ASSERT(channels <= maxChannels);

        // Buffer resize will safely handle data-migration and silences the structures
        for (const auto &buffer : m_channelBuffers) {
            buffer->resize(size);
        }
        m_channelBufferSize = size;

        m_interleavedBuffer.resize(size * maxChannels);
        m_interleavedBufferSize = (size * maxChannels);
    }

    // Dynamically secures memory capacity inside the raw byte container used for integer packaging
    void check_packed_byte_capacity(size_t requiredBytes) {
        if (m_packedByteBuffer.size() < requiredBytes) {
            m_packedByteBuffer.resize(requiredBytes);
        }
        std::memset(m_packedByteBuffer.data(), 0, m_packedByteBuffer.size());
    }

private:
    std::vector<std::unique_ptr<TAudioBuffer>>  m_channelBuffers;
    TAudioBuffer                                m_interleavedBuffer;
    std::vector<char>                           m_packedByteBuffer;

    uint                                        m_channelBufferSize;
    uint                                        m_interleavedBufferSize;
};
