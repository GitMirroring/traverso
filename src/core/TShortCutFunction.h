#ifndef TSHORTCUTFUNCTION_H
#define TSHORTCUTFUNCTION_H

#include <QStringList>
#include <QVariantList>

class TShortCutFunction {

public:
    explicit TShortCutFunction();
    ~TShortCutFunction();

    QString get_key_sequence(bool formatHtml=false);
    QString get_modifier_sequence(bool fromInheritedBase=true);
    QString get_slot_signature() const;
    QString get_description() const;
    QString get_inherited_base() const {return m_inheritedBase;}
    QString get_long_description() const;
    QList<int> get_modifier_keys(bool fromInheritedBase=true);
    QStringList get_keys(bool fromInheritedBase=true) const;

    const QMetaObject* get_metaobject() const {return m_metaObject;}

    TShortCutFunction* get_inherited_shortcut_function() const {return m_inheritedFunction;}

    int get_autorepeat_interval() const;
    int get_autorepeat_start_delay() const;

    bool uses_autorepeat() const {return m_usesAutoRepeat;}
    bool uses_inherited_base() const {return m_usesInheritedBase;}

    void set_metaobject(const QMetaObject* metaObject);

    void set_description(const QString& des);
    void set_inherited_base(const QString& base);
    void set_uses_inherited_base(bool b) {m_usesInheritedBase = b;}
    void set_autorepeat_interval(int interval) {m_autorepeatInterval = interval; m_usesAutoRepeat = true;}
    void set_autorepeat_start_delay(int delay) {m_autorepeatStartDelay = delay;}
    void set_slot_signature(const QString& signature) {slotsignature = signature;}

    static void make_shortcut_key_human_readable(QString& key, bool formatHtml=false);

    QVariantList arguments;
    QString pluginname;
    QString commandName;
    QString submenu;
    bool useX;
    bool useY;
    int sortorder;
    QString		slotsignature;
    QString		m_description;

private:
    const QMetaObject*  m_metaObject = nullptr;
    QStringList	m_keys;
    QString		m_inheritedBase;
    TShortCutFunction*	m_inheritedFunction;
    QList<int >	m_modifierkeys;
    int		m_autorepeatInterval;
    int		m_autorepeatStartDelay;
    bool		m_usesAutoRepeat;
    bool		m_usesInheritedBase;

    void set_inherited_shortcut_function(TShortCutFunction* inherited);


    friend class TShortCutManager;
};

#endif // TSHORTCUTFUNCTION_H
