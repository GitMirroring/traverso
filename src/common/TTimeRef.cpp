#include "TTimeRef.h"

#include <limits.h>

TTimeRef::TTimeRef() {
    m_position = 0;
}

TTimeRef::TTimeRef(nframes_t frame, uint rate) {
    Q_ASSERT(rate);
    m_position = (TTimeRef::UNIVERSAL_SAMPLE_RATE / rate) * frame;
}

TTimeRef TTimeRef::max_length()
{
    return TTimeRef(LLONG_MAX);
}

TTimeRef TTimeRef::negative_max_length()
{
    return TTimeRef((LLONG_MAX - 1) * -1);
}

TTimeRef::TTimeRef(qint64 position)
    : m_position(position)
{

}

TTimeRef::TTimeRef(double position)
    : m_position(qint64(position))
{

}
