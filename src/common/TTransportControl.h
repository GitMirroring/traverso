#ifndef TTRANSPORTCONTROL_H
#define TTRANSPORTCONTROL_H

#include "TTimeRef.h"

class TTransportControl
{
public:
    TTransportControl();

    // Same as Jackd transport states so we are compatible with Jack Audio Connection Kit
    typedef enum {
        /* the order matters for binary compatibility */
        TransportStopped = 0,       /**< Transport halted */
        TransportRolling = 1,       /**< Transport playing */
        TransportLooping = 2,       /**< For OLD_TRANSPORT, now ignored */
        TransportStarting = 3,      /**< Waiting for sync ready */
        TransportNetStarting = 4,       /**< Waiting for sync ready on the network*/

    } trav_transport_state_t;


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
