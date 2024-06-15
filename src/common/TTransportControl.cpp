#include "TTransportControl.h"

#include "TTimeRef.h"

TTransportControl::TTransportControl() {}

void TTransportControl::set_state(int state)
{
    m_state = state;
}

void TTransportControl::set_slave(bool slave)
{
    m_isSlave = slave;
}

void TTransportControl::set_realtime(bool realTime)
{
    m_isRealTime = realTime;
}

void TTransportControl::set_location(const TTimeRef &location)
{
    m_location = location;
}
