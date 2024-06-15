#ifndef TTRANSPORTCONTROL_H
#define TTRANSPORTCONTROL_H

#include "TTimeRef.h"

class TTransportControl
{
public:
    TTransportControl();

    enum {
        Stopped = 0,
        Rolling = 1,
        Looping = 2,
        Starting = 3
    };


    int get_state() const {return m_state;}
    bool is_slave() const {return m_isSlave;}
    bool is_realtime() const {return m_isRealTime;}
    TTimeRef get_location() const {return m_location;}

    void set_state(int state);
    void set_slave(bool slave);
    void set_realtime(bool realTime);
    void set_location(const TTimeRef& location);

private:
    int         m_state;
    bool        m_isSlave;
    bool        m_isRealTime;
    TTimeRef    m_location;
};

#endif // TTRANSPORTCONTROL_H
