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

#include "TChannelBuffer.h"
#include <memory>
#include <vector>

// Audio of several channels: one TChannelBuffer per channel, owned here,
// with a stable array of them for a resampler. Used where audio is read or
// written (TAudioReader, TAudioWriter, the disk streams), not on the audio
// thread: it grows. Who writes into it makes the room (check_capacity());
// the readers and writers keep their interleaved and packed scratch
// themselves. (Before 2026-10-28 this was TFileIOBuffer, and TAudioBuffer
// was the one-channel buffer, now TChannelBuffer: doc/design/renames.md.)
class TAudioBuffer {
public:
    TAudioBuffer()
        : m_channelBufferSize(1)
    {
        // Default initialize to stereo footprint capacity
        for (uint chan = 0; chan < 2; ++chan) {
            add_channel_buffer();
        }
    }

    // Accesses a single, standalone de-interleaved mono channel stream
    TChannelBuffer& get_channel_buffer(uint channel) {
        return *m_channelBuffers.at(channel);
    }
    const TChannelBuffer& get_channel_buffer(uint channel) const {
        return *m_channelBuffers.at(channel);
    }

    // The channel buffers, one per channel, as one array (a resampler takes
    // them so); they stay where they are when the buffer grows
    TChannelBuffer* const* channel_buffers() {
        return m_channelPointers.data();
    }
    const TChannelBuffer* const* channel_buffers() const {
        return m_channelPointers.data();
    }

    uint channel_count() const { return uint(m_channelBuffers.size()); }

    // Zeros in the first channels channel buffers, count frames from frame at
    void silence(nframes_t at, nframes_t count, uint channels) {
        for (uint channel = 0; channel < channels; ++channel) {
            m_channelBuffers.at(channel)->silence(at, count);
        }
    }

    // Room for size frames in each of channels channel buffers (frames, not
    // frames times channels). Who writes into the buffer makes the room: a
    // reader for what it reads. A source with more channels than the buffer
    // has gets the channel buffers it lacks. Grows only, and keeps what the
    // buffer held. Not in the audio thread: allocates
    void check_capacity(uint size, uint channels)
    {
        Q_ASSERT(size > 0);
        while (m_channelBuffers.size() < channels) {
            add_channel_buffer();
        }
        if (size > m_channelBufferSize) {
            // Buffer resize will safely handle data-migration and silences the structures
            for (const auto &buffer : m_channelBuffers) {
                buffer->resize(size);
            }
            m_channelBufferSize = size;
        }
    }

private:
    void add_channel_buffer() {
        m_channelBuffers.push_back(std::make_unique<TChannelBuffer>(m_channelBufferSize));
        m_channelPointers.push_back(m_channelBuffers.back().get());
    }

    std::vector<std::unique_ptr<TChannelBuffer>>  m_channelBuffers;
    std::vector<TChannelBuffer*>                  m_channelPointers;
    uint                                        m_channelBufferSize;
};
