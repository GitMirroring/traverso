#ifndef TVUMONITOR_H
#define TVUMONITOR_H

#include "APILinkedList.h"
#include "defines.h"


class TVUMonitor : public APILinkedListNode
{

public:
    TVUMonitor();
    ~TVUMonitor() {}

    virtual bool is_smaller_then(APILinkedListNode* /*node*/) { return true;}

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

private:
    bool    m_wasRead;
    audio_sample_t m_peak;
};

#endif // TVUMONITOR_H
