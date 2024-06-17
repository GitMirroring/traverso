#include "TShortCutFunction.h"


TShortCutFunction::TShortCutFunction() {
    m_inheritedFunction = nullptr;
    // used for cursor hinting. usually means displaying a horizontal arrow cursor
    // to indicate horizontal movement
    useX = false;
    // used for cursor hinting. usually means displaying a vertical arrow cursor
    // to indicate vertical movement
    useY = false;
    sortorder = 0;
    m_usesAutoRepeat = false;
    m_autorepeatInterval = -1;
    m_autorepeatStartDelay = -1;
    m_usesInheritedBase = false;
}
