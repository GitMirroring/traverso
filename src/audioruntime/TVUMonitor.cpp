#include "TVUMonitor.h"

TVUMonitor::TVUMonitor() {
    m_wasRead = 0;
    m_peak = 0;
}


/**
 *
 * @return The highest peak value since the previous call to this function,
 *		 call this at least 10 times each second to keep data consistent
 */



