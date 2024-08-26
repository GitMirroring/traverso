#include "TShortCutFunction.h"

#include "TShortCutManager.h"

#include <QTranslator>
#include "Debugger.h"
#include "Utils.h"

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

TShortCutFunction::~TShortCutFunction()
{
    printf("Deleting shortcut function %s\n", QS_C(commandName));
}

QString TShortCutFunction::get_modifier_sequence(bool fromInheritedBase)
{
    QString modifiersString;

    foreach(int modifier, get_modifier_keys(fromInheritedBase)) {
        switch(modifier) {
        case Qt::Key_Alt:
            modifiersString += "Alt+";
            break;
        case Qt::Key_Control:
            modifiersString += "Ctrl+";
            break;
        case Qt::Key_Shift:
            modifiersString += "Shift+";
            break;
        case Qt::Key_Meta:
            modifiersString += "Meta+";
            break;
        default:
            PERROR(QString("Unkown modifier key %1, programming error").arg(modifier));
        }
    }
    return modifiersString;
}

QString TShortCutFunction::get_key_sequence(bool formatHtml)
{
    QString sequence;
    QStringList sequenceList;
    QString modifiersString = get_modifier_sequence();

    if (get_modifier_keys().size())
    {
        modifiersString += " ";
    }

    foreach(QString keyString, get_keys())
    {

        sequenceList << (modifiersString + keyString);
    }

    sequence = sequenceList.join(" , ");

    TShortCutFunction::make_shortcut_key_human_readable(sequence, formatHtml);

    return sequence;
}

QList<int> TShortCutFunction::get_modifier_keys(bool fromInheritedBase)
{
    if (m_inheritedFunction && m_usesInheritedBase && fromInheritedBase)
    {
        return m_inheritedFunction->get_modifier_keys();
    }

    return m_modifierkeys;
}

QString TShortCutFunction::get_slot_signature() const
{
    // a slotsignature is only set for hold commands, not for modifier keys
    // if the shortcut is from a modifier key then the slotsignature will be
    // empty in which case we do return the slotsignature set for the TFunction
    if (m_inheritedFunction && !m_inheritedFunction->get_slot_signature().isEmpty())
    {
        return m_inheritedFunction->get_slot_signature();
    }

    return slotsignature;
}

QString TShortCutFunction::get_description() const
{
    if (!m_description.isEmpty())
    {
        return m_description;
    }

    if (m_inheritedFunction)
    {
        return m_inheritedFunction->get_description();
    }

    return m_description;
}

QString TShortCutFunction::get_long_description() const
{
    QString description = get_description();
    if (!submenu.isEmpty())
    {
        description = submenu + " : " + description;
    }
    return description;
}

void TShortCutFunction::set_description(const QString& description)
{
    m_description = description;
}

void TShortCutFunction::set_inherited_base(const QString &base)
{
    m_inheritedBase = base;
}

QStringList TShortCutFunction::get_keys(bool fromInheritedBase) const
{
    if (m_inheritedFunction && m_usesInheritedBase && fromInheritedBase)
    {
        return m_inheritedFunction->get_keys();
    }

    return m_keys;
}

void TShortCutFunction::set_inherited_shortcut_function(TShortCutFunction *inherited)
{
    m_inheritedFunction = inherited;
}

int TShortCutFunction::get_autorepeat_interval() const
{
    if (m_inheritedFunction && m_usesInheritedBase)
    {
        return m_inheritedFunction->get_autorepeat_interval();
    }

    return m_autorepeatInterval;
}

int TShortCutFunction::get_autorepeat_start_delay() const
{
    if (m_inheritedFunction && m_usesInheritedBase)
    {
        return m_inheritedFunction->get_autorepeat_start_delay();
    }

    return m_autorepeatStartDelay;
}

void TShortCutFunction::set_metaobject(const QMetaObject *metaObject)
{
    m_metaObject = metaObject;
}

void TShortCutFunction::make_shortcut_key_human_readable(QString& keyfact, bool formatHtml)
{
    keyfact.replace(QString("MOUSESCROLLVERTICALUP"), QObject::tr("Scroll Up"));
    keyfact.replace(QString("MOUSESCROLLVERTICALDOWN"), QObject::tr("Scroll Down"));
    keyfact.replace(QString("MOUSEBUTTONRIGHT"), QObject::tr("Right Button"));
    keyfact.replace(QString("MOUSEBUTTONLEFT"), QObject::tr("Left Button"));
    keyfact.replace(QString("MOUSEBUTTONMIDDLE"), QObject::tr("Center Button"));
    if (formatHtml) {
        keyfact.replace(QString("UPARROW"), QString("&uarr;"));
        keyfact.replace(QString("DOWNARROW"), QString("&darr;"));
        keyfact.replace(QString("LEFTARROW"), QString("&larr;"));
        keyfact.replace(QString("RIGHTARROW"), QString("&rarr;"));
        keyfact.replace(QString("PAGEDOWN"), "Page Up");
        keyfact.replace(QString("PAGEUP"), "Page Down");
        keyfact.replace(QString("MINUS"), QString("&#45;"));
        keyfact.replace(QString("PLUS"), QString("&#43;"));
    } else {
        keyfact.replace(QString("UPARROW"), QObject::tr("Up"));
        keyfact.replace(QString("DOWNARROW"), QObject::tr("Down"));
        keyfact.replace(QString("LEFTARROW"), QObject::tr("Left"));
        keyfact.replace(QString("RIGHTARROW"), QObject::tr("Right"));
        keyfact.replace(QString("PAGEDOWN"), "PgUp");
        keyfact.replace(QString("PAGEUP"), "PgDown");
        keyfact.replace(QString("MINUS"), "-");
        keyfact.replace(QString("PLUS"), "+");
    }
    keyfact.replace(QString("DELETE"), "Delete");
    keyfact.replace(QString("BKSPACE"), "Backspace");
    keyfact.replace(QString("ESC"), "Esc");
    keyfact.replace(QString("ENTER"), "Enter");
    keyfact.replace(QString("RETURN"), "Return");
    keyfact.replace(QString("SPACE"), QObject::tr("Space Bar"));
    keyfact.replace(QString("HOME"), "Home");
    keyfact.replace(QString("END"), "End");
    keyfact.replace(QString("NUMERICAL"), "0, 1, ... 9");
}
