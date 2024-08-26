#include "TShortCut.h"
#include "TShortCutFunction.h"

TShortCut::TShortCut(int keyValue)
{
    m_keyValue = keyValue;
}

TShortCut::~TShortCut()
{
}

QList<TShortCutFunction*> TShortCut::getFunctions()
{
    return m_dict.values();
}

QList<TShortCutFunction *> TShortCut::get_functions_for_metaobject(const QMetaObject *metaObject)
{
    return m_dict.values(metaObject);
}

void TShortCut::add_shortcut_function(TShortCutFunction *shortCutFunction)
{
    m_dict.insert(shortCutFunction->get_metaobject(), shortCutFunction);
}
