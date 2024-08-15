#ifndef TQUEUEBUFFERSLOT_H
#define TQUEUEBUFFERSLOT_H

#include "TAudioBuffer.h"
#include "defines.h"
#include "TTimeRef.h"

class TQueueBufferSlot {
public:
    explicit TQueueBufferSlot(int slotNumber, uint channelCount, nframes_t bufferSize) {
        m_fileLocation = TTimeRef::INVALID;
        m_transportLocation = TTimeRef::INVALID;
        m_slotNumber = slotNumber;
        m_bufferSize = bufferSize;
        m_bufferWriteOffset = 0;
        m_channelCount = channelCount;
        m_buffers.resize(channelCount, TAudioBuffer(bufferSize, false));
    }

    ~TQueueBufferSlot()
    {
    }

    int get_slot_number() const {return m_slotNumber;}
    inline nframes_t get_buffer_size() const {return m_bufferSize;}
    inline nframes_t get_read_nframes() const {return m_bufferSize - m_bufferWriteOffset;}
    inline TTimeRef get_file_location() const {return m_fileLocation;}
    inline TTimeRef get_transport_location() const {return m_transportLocation;}

    audio_sample_t* get_buffer(uint channel) {
        Q_ASSERT(channel < m_channelCount);
        return m_buffers.at(channel).get_buffer(m_bufferSize);
    }

    nframes_t get_buffer_write_offset() const {return m_bufferWriteOffset;}

    void read_buffer(audio_sample_t* dest, uint channel, nframes_t nframes, nframes_t offset = 0) {
        Q_ASSERT(nframes <= m_bufferSize);
        Q_ASSERT(offset + nframes <= m_bufferSize);
        Q_ASSERT(channel < m_channelCount);
        Q_ASSERT(nframes > 0);
        memcpy(dest + offset, m_buffers.at(channel).get_buffer(nframes), nframes * sizeof(audio_sample_t));
    }

    void write_buffer(const TTimeRef &transportLocation, const TTimeRef &fileLocation, audio_sample_t* source, uint channel, nframes_t nframes, nframes_t offset = 0) {
        Q_ASSERT(nframes <= m_bufferSize);
        Q_ASSERT(offset + nframes <= m_bufferSize);
        Q_ASSERT(channel < m_channelCount);
        Q_ASSERT(nframes > 0);
        memcpy(m_buffers.at(channel).get_buffer(nframes) + offset, source, nframes * sizeof(audio_sample_t));
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
        for (auto audioBuffer : m_buffers) {
            audioBuffer.silence_buffer();
        }
    }

private:
    TTimeRef            m_fileLocation;
    TTimeRef            m_transportLocation;
    int                 m_slotNumber;
    uint                m_channelCount;
    QList<TAudioBuffer> m_buffers;
    nframes_t           m_bufferSize;
    nframes_t           m_bufferWriteOffset;
};

#endif // TQUEUEBUFFERSLOT_H
