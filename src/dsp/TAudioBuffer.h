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
    TAudioBuffer() = default;

    // Accesses a single, standalone de-interleaved mono channel stream
    TChannelBuffer& get_channel_buffer(uint channel) {
        Q_ASSERT(channel < m_channelBuffers.size());
        return *m_channelBuffers[channel];
    }
    const TChannelBuffer& get_channel_buffer(uint channel) const {
        Q_ASSERT(channel < m_channelBuffers.size());
        return *m_channelBuffers[channel];
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
        Q_ASSERT(channels <= m_channelBuffers.size());
        for (uint channel = 0; channel < channels; ++channel) {
            m_channelBuffers[channel]->silence(at, count);
        }
    }

    // Room for frames frames in each of channels channel buffers (frames,
    // not frames times channels). Who writes into the buffer makes the room:
    // a reader for what it reads. No channels exist before the first call;
    // a source with more channels than the buffer has gets the channel
    // buffers it lacks. Grows only, and keeps what the buffer held. Not in
    // the audio thread: allocates
    void check_capacity(nframes_t frames, uint channels)
    {
        Q_ASSERT(frames > 0);
        if (frames > m_channelBufferSize) {
            for (const auto& buffer : m_channelBuffers) {
                buffer->resize(frames);
            }
            m_channelBufferSize = frames;
        }
        while (m_channelBuffers.size() < channels) {
            m_channelBuffers.push_back(std::make_unique<TChannelBuffer>(m_channelBufferSize));
            m_channelPointers.push_back(m_channelBuffers.back().get());
        }
    }

private:
    std::vector<std::unique_ptr<TChannelBuffer>>  m_channelBuffers;
    std::vector<TChannelBuffer*>                  m_channelPointers;
    nframes_t                                     m_channelBufferSize{0};
};
