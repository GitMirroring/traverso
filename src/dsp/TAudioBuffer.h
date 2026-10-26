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

#include "defines.h"
#include "TRealTime.h"
#include "Mixer.h"
#include "Utils.h"

#include <cstring>

class TAudioBuffer
{
public:
    // Whether the buffer's memory is kept in RAM (mlock()), so that the audio
    // thread never faults a page of it in: a TRealTimeAudioBuffer asks for it
    enum class MemoryLock { No, Yes };

    explicit TAudioBuffer(nframes_t size)
        : TAudioBuffer (size, MemoryLock::No)
    {
    }

    ~TAudioBuffer()
    {
        delete_buffer_data();
    }

    // Whether the memory is locked now: asked for and granted. A lock can fail;
    // the buffer then works unlocked
    bool is_memory_locked() const { return m_memLocked; }

    // Why a lock failed: no permission to lock memory (EPERM), or the limit
    // on locked memory reached (ENOMEM, EAGAIN)
    enum class MemoryLockFailure { NoPermission, LimitReached };
    // Called once per kind of failure, for the application to tell the user:
    // in the thread that made the buffer, or, for a failure from before the
    // handler was set, in the thread that sets it.
    using MemoryLockFailureHandler = void (*)(MemoryLockFailure failure);
    static void set_memory_lock_failure_handler(MemoryLockFailureHandler handler);

    // Not in the audio thread: allocates
    void resize(nframes_t size);

    // Silence whole buffer with zero's
    void silence_data() T_REALTIME_SAFE {
        memset (m_buffer, 0, sizeof (audio_sample_t) * m_size);
    }

    // Silence the first nframes frames with zero's
    void silence_data(nframes_t nframes) T_REALTIME_SAFE {
        Q_ASSERT(nframes <= m_size);
        memset (m_buffer, 0, sizeof (audio_sample_t) * nframes);
    }

    // The frames [at, at + count), range checked in a debug build
    audio_sample_t* data(nframes_t at, nframes_t count) T_REALTIME_SAFE {
        Q_ASSERT(at + count <= m_size);
        return m_buffer + at;
    }
    const audio_sample_t* data(nframes_t at, nframes_t count) const T_REALTIME_SAFE {
        Q_ASSERT(at + count <= m_size);
        return m_buffer + at;
    }

    // Zeros for count frames from frame at
    void silence(nframes_t at, nframes_t count) T_REALTIME_SAFE {
        Q_ASSERT(at + count <= m_size);
        memset(m_buffer + at, 0, sizeof(audio_sample_t) * count);
    }

    // The frames [at, at + count) multiplied by the first count values of
    // gains (a fade, a gain curve)
    void multiply(nframes_t at, nframes_t count, const TAudioBuffer& gains) T_REALTIME_SAFE {
        Q_ASSERT(at + count <= m_size && count <= gains.m_size);
        for (nframes_t frame = 0; frame < count; ++frame) {
            m_buffer[at + frame] *= gains.m_buffer[frame];
        }
    }

    // The data from frame 0, nframes of it range checked in a debug build
    audio_sample_t* get_data(nframes_t nframes) const T_REALTIME_SAFE {
        Q_ASSERT(nframes <= m_size);
        return m_buffer;
    }

    nframes_t get_size() const T_REALTIME_SAFE {
        return m_size;
    }

    // return by reference to the item @ index. Range check in Debug build
    audio_sample_t& operator[](nframes_t index) T_REALTIME_SAFE {
        Q_ASSERT(index < m_size);
        return m_buffer[index];
    }

    // return by value to the item @ index.  Range check in Debug build
    audio_sample_t at(nframes_t index) const T_REALTIME_SAFE {
        Q_ASSERT(index < m_size);
        return m_buffer[index];
    }

    // calculate peak value for the first nframes frames
    float compute_peak(nframes_t nframes, float current) T_REALTIME_SAFE
    {
        Q_ASSERT(nframes <= m_size);
        return Mixer::compute_peak(m_buffer, nframes, current);
    }

    // calculate peak value on whole buffer
    float compute_peak() T_REALTIME_SAFE
    {
        return Mixer::compute_peak(m_buffer, m_size, 0.0f);
    }

    static void mix_buffers_no_gain(const TAudioBuffer &dest, const TAudioBuffer &src, nframes_t nframes) T_REALTIME_SAFE {
        Mixer::mix_buffers_no_gain(dest.get_data(nframes), src.get_data(nframes), nframes);
    }

    static void mix_buffers_with_gain(const TAudioBuffer &dest, const TAudioBuffer &src, nframes_t nframes, float gain) T_REALTIME_SAFE {
        if (TraversoDAW::Float::equals_1(gain)) {
            return Mixer::mix_buffers_no_gain(dest.get_data(nframes), src.get_data(nframes), nframes);
        }

        Mixer::mix_buffers_with_gain(dest.get_data(nframes), src.get_data(nframes), nframes, gain);
    }

    // count frames of source, from its frame sourceAt, into this buffer from
    // frame at
    void copy_from(nframes_t at, const TAudioBuffer& source, nframes_t sourceAt, nframes_t count) T_REALTIME_SAFE {
        Q_ASSERT(at + count <= m_size && sourceAt + count <= source.m_size);
        memcpy(m_buffer + at, source.m_buffer + sourceAt, sizeof(audio_sample_t) * count);
    }

    static void copy_data(const TAudioBuffer &dest, const TAudioBuffer &src, nframes_t nframes) T_REALTIME_SAFE {
        memcpy(dest.get_data(nframes), src.get_data(nframes), nframes * sizeof(audio_sample_t));
    }

    void apply_gain_to_buffer(nframes_t nframes, float gain) T_REALTIME_SAFE {
        Q_ASSERT(nframes <= m_size);
        Mixer::apply_gain_to_buffer(m_buffer, nframes, gain);
    }

protected:
    // For TRealTimeAudioBuffer
    TAudioBuffer(nframes_t size, MemoryLock memoryLock)
        : m_memoryLock(memoryLock)
    {
        resize(size);
    }

private:
    static constexpr size_t AUDIO_BUFFER_ALIGNMENT = 64;

    void allocate_buffer_data(nframes_t size);
    void delete_buffer_data();

    audio_sample_t* m_buffer{nullptr};
    nframes_t       m_size{0};
    bool            m_memLocked{false};
    MemoryLock      m_memoryLock{MemoryLock::No};
};

// A buffer of the audio processing path: its memory locked in RAM. The type
// says so where the buffer is declared; it adds nothing else, and is never
// deleted through a TAudioBuffer pointer (no virtual destructor)
class TRealTimeAudioBuffer final : public TAudioBuffer
{
public:
    explicit TRealTimeAudioBuffer(nframes_t size) : TAudioBuffer(size, MemoryLock::Yes) {}
};

