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
        , m_size(size)
        , m_memLocked(false)
        , m_wantsMemLock(wantsMemLock)
    {
        if (size > 0) {
            resize(size);
        }
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
#ifdef USE_MLOCK
        if (m_memLocked) {
            if (munlock (m_buffer, m_size) == -1) {
                PERROR("Couldn't unlock buffer from memory");
            }
            m_memLocked = false;
        }
#endif /* USE_MLOCK */

        delete [] m_buffer;
        m_buffer = new audio_sample_t[size];
        m_size = size;

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

    inline audio_sample_t* get_buffer(nframes_t nframes) const {
        Q_ASSERT(nframes <= m_size);
        return m_buffer;
    }

    audio_sample_t& operator[](nframes_t index) {
        Q_ASSERT(index < m_size);
        return m_buffer[index];
    }

    float compute_peak(nframes_t nframes, float current)
    {
        Q_ASSERT(nframes <= m_size);
        return Mixer::compute_peak(m_buffer, nframes, current);
    }

    float compute_peak()
    {
        return Mixer::compute_peak(m_buffer, m_size, 0.0f);
    }

private:
    audio_sample_t* m_buffer;
    nframes_t       m_size;
    bool            m_memLocked;
    bool            m_wantsMemLock;
};

#endif // TAUDIOBUFFER_H
