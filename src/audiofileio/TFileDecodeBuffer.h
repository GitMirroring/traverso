#ifndef TFILEDECODEBUFFER_H
#define TFILEDECODEBUFFER_H


#include "TAudioBuffer.h"
#include "defines.h"

class TFileDecodeBuffer {

public:
    TFileDecodeBuffer()
    {
        for (uint chan=0; chan < 2; ++chan) {
            m_destinationBuffers.push_back(std::make_unique<TAudioBuffer>(0, true));
        }

        m_destinationBufferSize = m_readBufferSize = 0;
        m_destinationBufferReadOffset = 0;
    }

    ~TFileDecodeBuffer() {
    }

    uint get_destination_buffer_size() const {
        return m_destinationBufferSize;
    }

    audio_sample_t* get_destination_buffer(uint channel, nframes_t nframes) {
        return m_destinationBuffers.at(channel)->get_buffer(nframes + m_destinationBufferReadOffset) + m_destinationBufferReadOffset;
    }

    audio_sample_t* get_read_buffer(nframes_t nframes) {
        return m_readBuffer.get_buffer(nframes);
    }

    void set_destination_buffer_read_offset(nframes_t offset) {
        Q_ASSERT(offset < m_destinationBufferSize);
        m_destinationBufferReadOffset = offset;
    }

    void silence_buffers() {
        m_readBuffer.silence_buffer();
        for (const auto &buffer : m_destinationBuffers) {
            buffer->silence_buffer();
        }
    }

    void check_buffers_capacity(uint size, uint channels)
    {
        if (size < m_destinationBufferSize) {
            return;
        }

        uint myChannels = m_destinationBuffers.size();

        Q_ASSERT(channels <= myChannels);

        for (const auto &buffer : m_destinationBuffers) {
            buffer->resize(size);
        }
        m_destinationBufferSize = size;

        m_readBuffer.resize(size * myChannels);
        m_readBufferSize = (size * myChannels);
    }

private:
    std::vector<std::unique_ptr<TAudioBuffer>>  m_destinationBuffers;
    TAudioBuffer            m_readBuffer{0, true};
    uint                    m_destinationBufferSize;
    uint                    m_readBufferSize;
    uint                    m_destinationBufferReadOffset;
};

#endif // TFILEDECODEBUFFER_H
