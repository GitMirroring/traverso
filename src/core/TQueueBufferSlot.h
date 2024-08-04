#ifndef TQUEUEBUFFERSLOT_H
#define TQUEUEBUFFERSLOT_H

#include "defines.h"
#include "TTimeRef.h"

#if ! defined (Q_OS_WIN)
#include <sys/mman.h>
#endif

class TQueueBufferSlot {
public:
    TQueueBufferSlot(int slotNumber, uint channelCount, nframes_t bufferSize) {
        m_fileLocation = TTimeRef::INVALID;
        m_transportLocation = TTimeRef::INVALID;
        m_slotNumber = slotNumber;
        m_bufferSize = bufferSize;
        m_bufferWriteOffset = 0;
        m_channelCount = channelCount;
        m_mlocked = false;
        for (uint i=0; i<channelCount; ++i) {
            audio_sample_t* buf = new audio_sample_t[bufferSize];
            m_buffers.append(buf);
#ifdef USE_MLOCK
            if (mlock (buf, bufferSize) < 0) {
                printf("Unable to lock memory\n");
            } else {
                m_mlocked = true;
            }
#endif /* USE_MLOCK */
        }

        silence_buffers();
    }

    ~TQueueBufferSlot() {
        printf("destructor queuebufferslot\n");
        for (uint i=0; i<m_channelCount; ++i) {
            auto buf = m_buffers.at(i);
#ifdef USE_MLOCK
            if (m_mlocked) {
                munlock (buf, m_bufferSize);
            }
#endif /* USE_MLOCK */
            delete [] buf;
        }
    }

    int get_slot_number() const {return m_slotNumber;}
    inline nframes_t get_buffer_size() const {return m_bufferSize;}
    inline nframes_t get_read_nframes() const {return m_bufferSize - m_bufferWriteOffset;}
    inline TTimeRef get_file_location() const {return m_fileLocation;}
    inline TTimeRef get_transport_location() const {return m_transportLocation;}

    audio_sample_t* get_buffer(uint channel) {
        Q_ASSERT(channel < m_channelCount);
        return m_buffers.at(channel);
    }

    nframes_t get_buffer_write_offset() const {return m_bufferWriteOffset;}

    void read_buffer(audio_sample_t* dest, uint channel, nframes_t nframes, nframes_t offset = 0) {
        Q_ASSERT(nframes <= m_bufferSize);
        Q_ASSERT(offset + nframes <= m_bufferSize);
        Q_ASSERT(channel < m_channelCount);
        Q_ASSERT(nframes > 0);
        memcpy(dest + offset, m_buffers.at(channel), nframes * sizeof(audio_sample_t));
        // always zero content of complete buffer for now to avoid noise being played
        memset (m_buffers.at(channel), 0, sizeof (audio_sample_t) * m_bufferSize);
    }

    void write_buffer(const TTimeRef &transportLocation, const TTimeRef &fileLocation, audio_sample_t* source, uint channel, nframes_t nframes, nframes_t offset = 0) {
        Q_ASSERT(nframes <= m_bufferSize);
        Q_ASSERT(offset + nframes <= m_bufferSize);
        Q_ASSERT(channel < m_channelCount);
        Q_ASSERT(nframes > 0);
        memcpy(m_buffers.at(channel) + offset, source, nframes * sizeof(audio_sample_t));
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
        for (uint i=0; i<m_channelCount; ++i) {
            memset (m_buffers.at(i), 0, sizeof (audio_sample_t) * m_bufferSize);
        }
    }

private:
    TTimeRef            m_fileLocation;
    TTimeRef            m_transportLocation;
    int                 m_slotNumber;
    nframes_t           m_bufferSize;
    uint                m_channelCount;
    QList<audio_sample_t*>   m_buffers;
    nframes_t           m_bufferWriteOffset;
    bool                m_mlocked;
};

#endif // TQUEUEBUFFERSLOT_H
