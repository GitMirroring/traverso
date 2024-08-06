#ifndef TFILEDECODEBUFFER_H
#define TFILEDECODEBUFFER_H


#include "defines.h"

class TFileDecodeBuffer {

public:
    TFileDecodeBuffer()
    {
        m_readBuffer = nullptr;
        m_channelCount = m_destinationBufferSize = m_readBufferSize = 0;
        m_destinationBufferReadOffset = 0;
    }

    ~TFileDecodeBuffer() {
        delete_destination_buffers();
        delete_readbuffer();
    }

    uint get_destination_buffer_size() const {
        return m_destinationBufferSize;
    }

    audio_sample_t* get_destination_buffer(uint channel, nframes_t nframes) {
        Q_ASSERT((nframes + m_destinationBufferReadOffset) <= m_destinationBufferSize);
        Q_ASSERT(channel < m_channelCount);
        return m_destinationBuffers.at(channel) + m_destinationBufferReadOffset;
    }

    audio_sample_t* get_read_buffer(nframes_t nframes) {
        Q_ASSERT(nframes <= m_readBufferSize);
        return m_readBuffer;
    }

    void set_destination_buffer_read_offset(nframes_t offset) {
        Q_ASSERT(offset < m_destinationBufferSize);
        m_destinationBufferReadOffset = offset;
    }

    void silence_buffers() {
        memset (m_readBuffer, 0, sizeof (audio_sample_t) * m_readBufferSize);
        for (uint channel = 0; channel < m_channelCount; ++channel) {
            memset (m_destinationBuffers.at(channel), 0, sizeof (audio_sample_t) * m_destinationBufferSize);
        }
    }

    void check_buffers_capacity(uint size, uint channels)
    {
        /*	m_bufferSizeCheckCounter++;
    m_totalCheckSize += size;

    float meanvalue = (m_totalCheckSize / (float)m_bufferSizeCheckCounter);

    if (meanvalue < destinationBufferSize && ((meanvalue + 256) < destinationBufferSize) && !(destinationBufferSize == size)) {
        m_smallerReadCounter++;
        if (m_smallerReadCounter > 8) {
            delete_destination_buffers();
            delete_readbuffer();
            m_bufferSizeCheckCounter = m_smallerReadCounter = 0;
            m_totalCheckSize = 0;
        }
    }*/


        if (m_destinationBufferSize < size || m_channelCount < channels) {

            delete_destination_buffers();

            m_channelCount = channels;

            for (uint chan = 0; chan < m_channelCount; chan++) {
                m_destinationBuffers.append(new audio_sample_t[size]);
            }

            m_destinationBufferSize = size;
            // 		printf("resizing destination to %.3f KB\n", (float)size*4/1024);
        }

        if (m_readBufferSize < (size*m_channelCount)) {

            delete_readbuffer();

            m_readBuffer = new audio_sample_t[size*m_channelCount];
            m_readBufferSize = (size*m_channelCount);
        }

        silence_buffers();
    }

private:
    QList<audio_sample_t*>  m_destinationBuffers;
    audio_sample_t*         m_readBuffer;
    uint                    m_channelCount;
    uint                    m_destinationBufferSize;
    uint                    m_readBufferSize;
    uint                    m_destinationBufferReadOffset;

    void delete_destination_buffers()
    {
        while(!m_destinationBuffers.isEmpty()) {
            delete [] m_destinationBuffers.takeLast();
        }

        m_destinationBufferSize = 0;
        m_channelCount = 0;
    }

    void delete_readbuffer()
    {
        if (!m_readBuffer) {
            return;
        }

        delete [] m_readBuffer;

        m_readBuffer = nullptr;
        m_readBufferSize = 0;

    }


};

#endif // TFILEDECODEBUFFER_H
