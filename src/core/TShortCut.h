#ifndef TSHORTCUT_H
#define TSHORTCUT_H

#include <QList>
#include <QMultiHash>

class TShortCutFunction;

class TShortCut
{
public:
    TShortCut(int keyValue);
    ~ TShortCut();

    int getKeyValue() const {return m_keyValue;}

    QList<TShortCutFunction*> getFunctionsForObject(const QString& objectName);
    QList<TShortCutFunction*> getFunctions();

    int		autorepeatInterval;
    int		autorepeatStartDelay;

private:
    QMultiHash<QString, TShortCutFunction*> objects;
    int		m_keyValue;


    friend class TShortCutManager;
};

#endif // TSHORTCUT_H
