#ifndef TPROCESSCALLBACKDATA_H
#define TPROCESSCALLBACKDATA_H

#include "TTimeRef.h"
#include "defines.h"

class AudioBus;

class TProcessCallBackData {

public:
    TProcessCallBackData() {
        m_ringBufferReadBus = nullptr;
        m_nFramesToProcess = 0;
        m_startLocation = TTimeRef();
        m_endLocation = TTimeRef();
        m_ringBufferReadWaitTime = 0;
        m_realTime = true;
        m_sampleRate = 0;
    }

    void set_ringbuffer_read_bus(AudioBus* bus) {
        m_ringBufferReadBus = bus;
    }
    void set_start_location(const TTimeRef& location) {
        m_startLocation = location;
        m_endLocation = m_startLocation + TTimeRef(m_nFramesToProcess, m_sampleRate);
    }

    AudioBus* get_ringbuffer_read_bus() const {
        Q_ASSERT(m_ringBufferReadBus);
        return m_ringBufferReadBus;
    }
    nframes_t get_nframes_to_process() const {return m_nFramesToProcess;}
    TTimeRef get_start_location() const {return m_startLocation;}
    TTimeRef get_end_location() const {return m_endLocation;}
    bool get_is_real_time() const {return m_realTime;}
    uint get_sample_rate() const {
        Q_ASSERT(m_sampleRate != 0);
        return m_sampleRate;
    }

    void add_ringbuffer_read_wait_time(trav_time_t time) {
        m_ringBufferReadWaitTime += time;
    }

private:
    AudioBus*   m_ringBufferReadBus;
    nframes_t   m_nFramesToProcess;
    TTimeRef    m_startLocation;
    TTimeRef    m_endLocation;
    trav_time_t m_ringBufferReadWaitTime;
    bool        m_realTime;
    uint        m_sampleRate;

    void set_nframes_to_process(nframes_t nframes) {
        m_nFramesToProcess = nframes;
    }
    void set_real_time(bool realTime) {
        m_realTime = realTime;
    }
    void set_sample_rate(uint rate) {
        m_sampleRate= rate;
    }
    trav_time_t get_ringbuffer_read_wait_time() {
        auto time = m_ringBufferReadWaitTime;
        m_ringBufferReadWaitTime = 0;
        return time;
    }

    friend class AudioDevice;

};

#endif // TPROCESSCALLBACKDATA_H
