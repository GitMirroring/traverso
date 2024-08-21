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
    explicit TAudioBuffer(nframes_t size)
        : TAudioBuffer (size, false)
    {
    }

    virtual ~TAudioBuffer()
    {
        delete_buffer_data();
    }

    void resize(nframes_t size) {
        Q_ASSERT(size > 0);

        if (size == m_size) {
            return;
        }

        delete_buffer_data();

        allocate_buffer_data(size);

        silence_data();
    }

    // Silence whole buffer with zero's, read offset is discarded
    void silence_data() {
        memset (m_buffer, 0, sizeof (audio_sample_t) * m_size);
    }

    // Silence buffer with zero's for nframes, starting at read offset (if read offset was set before this call)
    void silence_data(nframes_t nframes) {
        Q_ASSERT((nframes + m_readOffset) <= m_size);
        memset (m_buffer + m_readOffset, 0, sizeof (audio_sample_t) * nframes);
    }

    // returns pointer to data buffer starting from read offset (if it was set). Range check in Debug build
    audio_sample_t* get_data(nframes_t nframes) const {
        Q_ASSERT((nframes + m_readOffset) <= m_size);
        return m_buffer + m_readOffset;
    }

    nframes_t get_size() const {
        return m_size;
    }

    // return by reference to the item @ index. Range check in Debug build
    audio_sample_t& operator[](nframes_t index) {
        Q_ASSERT((index + m_readOffset) < m_size);
        return m_buffer[index + m_readOffset];
    }

    // return by value to the item @ index.  Range check in Debug build
    audio_sample_t at(nframes_t index) const {
        Q_ASSERT((index + m_readOffset) < m_size);
        return m_buffer[index + m_readOffset];
    }

    // calculate peak value for nframes starting at readOffset if it was set
    float compute_peak(nframes_t nframes, float current)
    {
        Q_ASSERT((nframes + m_readOffset) <= m_size);
        return Mixer::compute_peak(m_buffer + m_readOffset, nframes, current);
    }

    // calculate peak value on whole buffer, discarding readOffset if it was set
    float compute_peak()
    {
        return Mixer::compute_peak(m_buffer, m_size, 0.0f);
    }

    static void mix_buffers_no_gain(const TAudioBuffer &dest, const TAudioBuffer &src, nframes_t nframes) {
        Mixer::mix_buffers_no_gain(dest.get_data(nframes), src.get_data(nframes), nframes);
    }

    static void mix_buffers_with_gain(const TAudioBuffer &dest, const TAudioBuffer &src, nframes_t nframes, float gain) {
        if (gain == 1.0f) {
            return Mixer::mix_buffers_no_gain(dest.get_data(nframes), src.get_data(nframes), nframes);
        }

        Mixer::mix_buffers_with_gain(dest.get_data(nframes), src.get_data(nframes), nframes, gain);
    }

    static void copy_data(const TAudioBuffer &dest, const TAudioBuffer &src, nframes_t nframes) {
        memcpy(dest.get_data(nframes), src.get_data(nframes), nframes * sizeof(audio_sample_t));
    }

    void apply_gain_to_buffer(nframes_t nframes, float gain) {
        Q_ASSERT((nframes + m_readOffset) <= m_size);
        Mixer::apply_gain_to_buffer(m_buffer + m_readOffset, nframes, gain);
    }

    void set_data_start_offset(nframes_t offset) {
        Q_ASSERT(offset < m_size);
        m_readOffset = offset;
    }

private:
    friend class TRealTimeAudioBuffer;
    explicit TAudioBuffer(nframes_t size, bool wantsMemLock)
        : m_buffer(nullptr)
        , m_size(0)
        , m_readOffset(0)
        , m_memLocked(false)
        , m_wantsMemLock(wantsMemLock)
    {
        resize(size);
    }

    audio_sample_t* m_buffer;
    nframes_t       m_size;
    nframes_t       m_readOffset;
    bool            m_memLocked;
    bool            m_wantsMemLock;

    void allocate_buffer_data(nframes_t size) {
        m_buffer = new audio_sample_t[size];
        m_size = size;

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

    void delete_buffer_data() {
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
    }

};

class TRealTimeAudioBuffer : public TAudioBuffer
{
public:
    TRealTimeAudioBuffer(nframes_t size) : TAudioBuffer(size, true) {}
    ~TRealTimeAudioBuffer() {}
};

#endif // TAUDIOBUFFER_H
