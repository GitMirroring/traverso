#ifndef TVUMONITOR_H
#define TVUMONITOR_H

#include "defines.h"


class TVUMonitor
{

public:
    TVUMonitor();
    ~TVUMonitor() {}

    inline void process(float peakValue) {
        if (m_wasRead) {
            m_peak = 0.0f;
            m_wasRead = false;
        }

        if (peakValue > m_peak) {
            m_peak = peakValue;
        }
    }

    inline audio_sample_t get_peak_value()
    {
        if (m_wasRead) {
            return 0.0;
        }

        float result = m_peak;

        return result;
    }

    inline void set_read() {m_wasRead = true;}

    bool operator<(const TVUMonitor& /*other*/) {
        return false;
    }
    TVUMonitor* next = nullptr;

private:
    bool    m_wasRead;
    audio_sample_t m_peak;
};

#endif // TVUMONITOR_H
