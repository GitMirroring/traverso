#include "TShortCut.h"

TShortCut::TShortCut(int keyValue)
{
    m_keyValue = keyValue;
}

TShortCut::~TShortCut()
{


}

QList<TShortCutFunction*> TShortCut::getFunctionsForObject(const QString &objectName)
{
    return objects.values(objectName);
}

QList<TShortCutFunction*> TShortCut::getFunctions()
{
    return objects.values();
}
