#include "TTimeRef.h"

TTimeRef::TTimeRef() {
    m_position = 0;
}

TTimeRef::TTimeRef(nframes_t frame, uint rate) {
    Q_ASSERT(rate);
    m_position = (UNIVERSAL_SAMPLE_RATE / rate) * frame;
}

TTimeRef::TTimeRef(qint64 position)
    : m_position(position)
{

}

TTimeRef::TTimeRef(double position)
    : m_position(qint64(position))
{

}
