#ifndef TSHORTCUTFUNCTION_H
#define TSHORTCUTFUNCTION_H

#include <QStringList>
#include <QVariantList>

class TShortCutFunction {

public:
    TShortCutFunction();

    QString getKeySequence(bool formatHtml=false);
    QString getModifierSequence(bool fromInheritedBase=true);
    QString getSlotSignature() const;
    QString getDescription() const;
    QString getInheritedBase() const {return m_inheritedBase;}
    QString getLongDescription() const;
    QList<int> getModifierKeys(bool fromInheritedBase=true);
    QStringList getKeys(bool fromInheritedBase=true) const;
    QStringList getObjects() const;
    QString getObject() const;

    int getAutoRepeatInterval() const;
    int getAutoRepeatStartDelay() const;
    TShortCutFunction* getInheritedFunction() const {return m_inheritedFunction;}


    bool usesAutoRepeat() const {return m_usesAutoRepeat;}
    bool usesInheritedBase() const {return m_usesInheritedBase;}

    void setDescription(const QString& des);
    void setInheritedBase(const QString& base);
    void setUsesInheritedbase(bool b) {m_usesInheritedBase = b;}
    void setAutoRepeatInterval(int interval) {m_autorepeatInterval = interval; m_usesAutoRepeat = true;}
    void setAutoRepeatStartDelay(int delay) {m_autorepeatStartDelay = delay;}
    void setSlotSignature(const QString& signature) {slotsignature = signature;}

    static bool smaller(const TShortCutFunction* left, const TShortCutFunction* right )
    {
        return left->sortorder < right->sortorder;
    }
    static bool greater(const TShortCutFunction* left, const TShortCutFunction* right )
    {
        return left->sortorder > right->sortorder;
    }

    static void makeShortcutKeyHumanReadable(QString& key, bool formatHtml=false);

    QVariantList arguments;
    QString object;
    QString pluginname;
    QString commandName;
    QString submenu;
    bool useX;
    bool useY;
    int sortorder;

private:
    QStringList	m_keys;
    QString		slotsignature;
    QString		m_description;
    QString		m_inheritedBase;
    TShortCutFunction*	m_inheritedFunction;
    QList<int >	m_modifierkeys;
    int		m_autorepeatInterval;
    int		m_autorepeatStartDelay;
    bool		m_usesAutoRepeat;
    bool		m_usesInheritedBase;

    void setInheritedFunction(TShortCutFunction* inherited);


    friend class TShortCutManager;
};

#endif // TSHORTCUTFUNCTION_H
