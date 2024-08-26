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

    int get_key_value() const {return m_keyValue;}

    QList<TShortCutFunction*> getFunctions();
    QList<TShortCutFunction*> get_functions_for_metaobject(const QMetaObject *metaObject);

    void add_shortcut_function(TShortCutFunction* shortCutFunction);

    int		autorepeatInterval;
    int		autorepeatStartDelay;

private:
    QMultiHash<const QMetaObject*, TShortCutFunction*> m_dict;
    int		m_keyValue;
};

#endif // TSHORTCUT_H
