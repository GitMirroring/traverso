#include "TShortCutFunction.h"

#include "TShortCutManager.h"

#include <QTranslator>
#include "Debugger.h"

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

QString TShortCutFunction::getModifierSequence(bool fromInheritedBase)
{
    QString modifiersString;

    foreach(int modifier, getModifierKeys(fromInheritedBase)) {
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

QString TShortCutFunction::getKeySequence(bool formatHtml)
{
    QString sequence;
    QStringList sequenceList;
    QString modifiersString = getModifierSequence();

    if (getModifierKeys().size())
    {
        modifiersString += " ";
    }

    foreach(QString keyString, getKeys())
    {

        sequenceList << (modifiersString + keyString);
    }

    sequence = sequenceList.join(" , ");

    TShortCutFunction::makeShortcutKeyHumanReadable(sequence, formatHtml);

    return sequence;
}

QList<int> TShortCutFunction::getModifierKeys(bool fromInheritedBase)
{
    if (m_inheritedFunction && m_usesInheritedBase && fromInheritedBase)
    {
        return m_inheritedFunction->getModifierKeys();
    }

    return m_modifierkeys;
}

QString TShortCutFunction::getSlotSignature() const
{
    // a slotsignature is only set for hold commands, not for modifier keys
    // if the shortcut is from a modifier key then the slotsignature will be
    // empty in which case we do return the slotsignature set for the TFunction
    if (m_inheritedFunction && !m_inheritedFunction->getSlotSignature().isEmpty())
    {
        return m_inheritedFunction->getSlotSignature();
    }

    return slotsignature;
}

QString TShortCutFunction::getDescription() const
{
    if (!m_description.isEmpty())
    {
        return m_description;
    }

    if (m_inheritedFunction)
    {
        return m_inheritedFunction->getDescription();
    }

    return m_description;
}

QString TShortCutFunction::getLongDescription() const
{
    QString description = getDescription();
    if (!submenu.isEmpty())
    {
        description = submenu + " : " + description;
    }
    return description;
}

void TShortCutFunction::setDescription(const QString& description)
{
    m_description = description;
}

void TShortCutFunction::setInheritedBase(const QString &base)
{
    m_inheritedBase = base;
}

QStringList TShortCutFunction::getKeys(bool fromInheritedBase) const
{
    if (m_inheritedFunction && m_usesInheritedBase && fromInheritedBase)
    {
        return m_inheritedFunction->getKeys();
    }

    return m_keys;
}

QStringList TShortCutFunction::getObjects() const
{
    return object.split("::", Qt::SkipEmptyParts);
}

QString TShortCutFunction::getObject() const
{
    return getObjects().first();
}

void TShortCutFunction::setInheritedFunction(TShortCutFunction *inherited)
{
    m_inheritedFunction = inherited;
}

int TShortCutFunction::getAutoRepeatInterval() const
{
    if (m_inheritedFunction && m_usesInheritedBase)
    {
        return m_inheritedFunction->getAutoRepeatInterval();
    }

    return m_autorepeatInterval;
}

int TShortCutFunction::getAutoRepeatStartDelay() const
{
    if (m_inheritedFunction && m_usesInheritedBase)
    {
        return m_inheritedFunction->getAutoRepeatStartDelay();
    }

    return m_autorepeatStartDelay;
}

void TShortCutFunction::makeShortcutKeyHumanReadable(QString& keyfact, bool formatHtml)
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
