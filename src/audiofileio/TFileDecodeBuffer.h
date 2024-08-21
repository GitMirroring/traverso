#ifndef TFILEDECODEBUFFER_H
#define TFILEDECODEBUFFER_H


#include "TAudioBuffer.h"
#include "defines.h"

class TFileDecodeBuffer {

public:
    TFileDecodeBuffer() : m_readBuffer(1, true)
    {
        for (uint chan=0; chan < 2; ++chan) {
            m_destinationBuffers.push_back(std::make_unique<TAudioBuffer>(1, true));
        }

        m_destinationBufferSize = m_readBufferSize = 1;
    }

    ~TFileDecodeBuffer() {
    }

    uint get_destination_buffer_size() const {
        return m_destinationBufferSize;
    }

    TAudioBuffer& get_destination_buffer(uint channel) {
        return *m_destinationBuffers.at(channel);
    }

    TAudioBuffer& get_read_buffer() {
        return m_readBuffer;
    }

    void silence_buffers() {
        m_readBuffer.silence_data();
        for (const auto &buffer : m_destinationBuffers) {
            buffer->silence_data();
        }
    }

    void set_destination_buffer_read_offset(nframes_t nframes) {
        for (const auto &buffer : m_destinationBuffers) {
            buffer->set_read_offset(nframes);
        }
    }

    void check_buffers_capacity(uint size, uint channels)
    {
        Q_ASSERT(size > 0);

        if (size <= m_destinationBufferSize) {
            return;
        }

        uint myChannels = m_destinationBuffers.size();

        Q_ASSERT(channels <= myChannels);

        // Buffer resize will silence the buffers for us
        for (const auto &buffer : m_destinationBuffers) {
            buffer->resize(size);
        }
        m_destinationBufferSize = size;

        m_readBuffer.resize(size * myChannels);
        m_readBufferSize = (size * myChannels);
    }

private:
    std::vector<std::unique_ptr<TAudioBuffer>>  m_destinationBuffers;
    TAudioBuffer            m_readBuffer;
    uint                    m_destinationBufferSize;
    uint                    m_readBufferSize;
};

#endif // TFILEDECODEBUFFER_H
