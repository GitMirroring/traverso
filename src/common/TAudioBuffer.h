#ifndef TAUDIOBUFFER_H
#define TAUDIOBUFFER_H

#include "Debugger.h"
#include "defines.h"
#include "Mixer.h"

#ifdef USE_MLOCK
#include <sys/mman.h>
#endif /* USE_MLOCK */

class TAudioBuffer
{
public:
    explicit TAudioBuffer(nframes_t size, bool wantsMemLock)
        : m_buffer(nullptr)
        , m_size(0)
        , m_readOffset(0)
        , m_memLocked(false)
        , m_wantsMemLock(wantsMemLock)
    {
        resize(size);
    }

    ~TAudioBuffer()
    {
#ifdef USE_MLOCK

        if (m_memLocked) {
            munlock (m_buffer, m_size);
        }
#endif /* USE_MLOCK */

        delete [] m_buffer;
    }

    void resize(nframes_t size) {
        if (m_size == size) {
            return;
        }

#ifdef USE_MLOCK
        if (m_memLocked) {
            if (munlock (m_buffer, m_size) == -1) {
                PERROR("Couldn't unlock buffer from memory");
            }
            m_memLocked = false;
        }
#endif /* USE_MLOCK */

        delete [] m_buffer;
        m_buffer = nullptr;
        m_size = size;

        if (m_size == 0) {
            return;
        }

        m_buffer = new audio_sample_t[size];

        silence_buffer();

#ifdef USE_MLOCK
        if (m_wantsMemLock) {
            if (mlock (m_buffer, size) == -1) {
                PERROR("Couldn't lock buffer into memory");
            } else {
                m_memLocked = true;
            }
        }
#endif /* USE_MLOCK */
    }

    void silence_buffer() {
        memset (m_buffer, 0, sizeof (audio_sample_t) * m_size);
    }

    audio_sample_t* get_buffer(nframes_t nframes) const {
        Q_ASSERT((nframes + m_readOffset) <= m_size);
        return m_buffer + m_readOffset;
    }

    audio_sample_t& operator[](nframes_t index) {
        Q_ASSERT((index + m_readOffset) < m_size);
        return m_buffer[index + m_readOffset];
    }

    audio_sample_t at(nframes_t index) const {
        Q_ASSERT((index + m_readOffset) < m_size);
        return m_buffer[index + m_readOffset];
    }

    float compute_peak(nframes_t nframes, float current)
    {
        Q_ASSERT(nframes <= m_size);
        return Mixer::compute_peak(m_buffer + m_readOffset, nframes, current);
    }

    float compute_peak()
    {
        return Mixer::compute_peak(m_buffer, m_size, 0.0f);
    }

    void mix_buffer_no_gain(TAudioBuffer &other, nframes_t nframes) {
        Mixer::mix_buffers_no_gain(get_buffer(nframes), other.get_buffer(nframes), nframes);
    }

    void mix_buffer_with_gain(TAudioBuffer &other, nframes_t nframes, float gain) {
        Mixer::mix_buffers_with_gain(get_buffer(nframes), other.get_buffer(nframes), nframes, gain);
    }

    void copy_buffer(TAudioBuffer &other, nframes_t nframes) {
        memcpy(get_buffer(nframes), other.get_buffer(nframes), nframes * sizeof(audio_sample_t));
    }

    void apply_gain_to_buffer(nframes_t nframes, float gain) {
        Q_ASSERT((nframes + m_readOffset) <= m_size);
        Mixer::apply_gain_to_buffer(m_buffer + m_readOffset, nframes, gain);
    }

    void set_read_offset(nframes_t offset) {
        Q_ASSERT(offset < m_size);
        m_readOffset = offset;
    }

private:
    audio_sample_t* m_buffer;
    nframes_t       m_size;
    nframes_t       m_readOffset;
    bool            m_memLocked;
    bool            m_wantsMemLock;
};

#endif // TAUDIOBUFFER_H
