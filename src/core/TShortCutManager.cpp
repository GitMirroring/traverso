/*
Copyright (C) 2011 Remon Sijrier

This file is part of Traverso

Traverso is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA.

*/

#include "TShortCutManager.h"
#include "TShortCut.h"
#include "TShortCutFunction.h"

#include <QPluginLoader>
#include <QSettings>
#include <QApplication>
#include <QMap>
#include <QPalette>
#include <QTextDocument>
#include <QDir>
#include <QTextStream>

#include "PCommand.h"
#include "TContextItem.h"
#include "TInformUser.h"
#include "Utils.h"
#include "TCommandPlugin.h"
#include "TConfig.h"

#include "Debugger.h"

TShortCutManager* manager = nullptr;

TShortCutManager& tShortCutManager()
{
    if (!manager) {
        manager = new TShortCutManager;
    }
    return *manager;
}

TShortCutManager::TShortCutManager()
{
	cpointer().add_contextitem(this);
}

TShortCutManager::~TShortCutManager()
{
    for(TShortCut* shortCut : std::as_const(m_shortcuts))
    {
        delete shortCut;
    }
    for(TShortCutFunction* function : std::as_const(m_functions)) {
        delete function;
    }
}

void TShortCutManager::register_shortcut_function(TShortCutFunction *function)
{
    Q_ASSERT(function->get_metaobject());
    Q_ASSERT(!function->commandName.isEmpty());

	if (m_functions.contains(function->commandName))
	{
		printf("There is already a function registered with command name %s\n", QS_C(function->commandName));
		return;
	}

	m_functions.insert(function->commandName, function);
}

TShortCutFunction* TShortCutManager::get_shortcut_function(const QString &functionName) const
{
    TShortCutFunction* function = m_functions.value(functionName, nullptr);
	if (!function)
	{
        printf("TShortCutManager::getFunction: Function %s not in database!!\n", functionName.toLatin1().data());
	}

	return function;
}

QList< TShortCutFunction* > TShortCutManager::get_shortcut_function_for_class(QString className)
{
    QList<TShortCutFunction* > functionsList;
    QStringList classes = m_classes.value(className.remove("View"));
    for(const QString &objectName : classes)
	{
        for(TShortCutFunction* function : m_functions)
		{
			// filter out objects that inherit from MoveCommand
			// but do not support move up/down
			bool hasRequiredSlot = true;
			if (!function->slotsignature.isEmpty())
			{
				QList<const QMetaObject*> metaList = m_metaObjects.value(className);
				if (metaList.size())
				{
					if (function->slotsignature == "move_up" && metaList.first()->indexOfMethod("move_up(bool)") == -1) {
						hasRequiredSlot = false;
					}
					if (function->slotsignature == "move_down" && metaList.first()->indexOfMethod("move_down(bool)") == -1) {
						hasRequiredSlot = false;
					}
				}
			}

            if (function->get_metaobject()->className() == objectName && hasRequiredSlot)
			{
				functionsList.append(function);
			}
		}
	}

    std::sort(functionsList.begin(), functionsList.end(), [&](TShortCutFunction* left, TShortCutFunction* right) {
        return left->sortorder < right->sortorder;
    });

    return functionsList;
}

TShortCut* TShortCutManager::get_shortcut_for_key(const QString &keyString)
{
    int keyValue;

    if (!keyboard_key_string_to_numerical_value(keyString, keyValue)) {
	       tInformUser().warning(tr("Shortcut Manager: Loaded keymap has this unrecognized key: %1").arg(keyString));
           return nullptr;
	}

    TShortCut* shortcut = m_shortcuts.value(keyValue, nullptr);

	if (!shortcut)
	{
        shortcut = new TShortCut(keyValue);
		m_shortcuts.insert(keyValue, shortcut);
	}

	return shortcut;
}

TShortCut* TShortCutManager::get_shortcut_for_key(int key)
{
    TShortCut* shortcut = m_shortcuts.value(key, nullptr);
	return shortcut;
}

TCommandPlugin* TShortCutManager::get_command_plugin(const QString &pluginName)
{
	return m_commandPlugins.value(pluginName);
}

void TShortCutManager::register_command_plugin(TCommandPlugin *plugin, const QString &pluginName)
{
    plugin->load();

    m_commandPlugins.insert(pluginName, plugin);
}

bool TShortCutManager::is_command_class(const QString &className)
{
	QList<const QMetaObject*> list = m_metaObjects.value(className);

	// A Command class only has one metaobject, compared to a
	// core + its view item which equals 2 metaobjects for just one 'object'
	if (list.size() == 1)
	{
        return class_inherits(list.at(0)->className(), "TCommand");
	}

	return false;
}

QList<QString> TShortCutManager::get_class_names() const
{
    return m_classes.keys();
}

QString TShortCutManager::get_class_for_object(const QMetaObject *metaObject) const
{
	QStringList keys = m_classes.keys();
    for(const QString &key : keys)
	{
		QStringList objects = m_classes.value(key);
        if (objects.contains(metaObject->className()))
		{
			return key;
		}
	}
	return "";
}

void TShortCutManager::save_shortcut_function(TShortCutFunction *function)
{
    QList<TShortCutFunction*> list;
    list << function;
    return save_shortcut_fuctions(list);
}

void TShortCutManager::save_shortcut_fuctions(QList<TShortCutFunction *> functions)
{
    PENTER;
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "Traverso", "Shortcuts");


    for(TShortCutFunction* function : functions)
    {
        QStringList modifiers = function->get_modifier_sequence(false).split("+", Qt::SkipEmptyParts);

        settings.beginGroup(function->commandName);
        settings.setValue("keys", function->get_keys(false).join(";"));
        settings.setValue("modifiers", modifiers.join(";"));
        settings.setValue("sortorder", function->sortorder);

        if (!function->submenu.isEmpty())
        {
            settings.setValue("submenu", function->submenu);
        }
        if (function->get_autorepeat_interval() >= 0)
        {
            settings.setValue("autorepeatinterval", function->get_autorepeat_interval());
        }
        if (function->get_autorepeat_start_delay() >= 0)
        {
            settings.setValue("autorepeatstartdelay", function->get_autorepeat_start_delay());
        }
        if (function->get_inherited_shortcut_function())
        {
            if (function->uses_inherited_base())
            {
                settings.setValue("usesinheritedbase", true);
            }
            else
            {
                settings.setValue("usesinheritedbase", false);
            }
        }
        settings.endGroup();
    }
}

void TShortCutManager::export_functions()
{
    PENTER;
    save_shortcut_fuctions(m_functions.values());
}

void TShortCutManager::load_shortcuts()
{
    PENTER;
    for(TShortCut* shortCut : std::as_const(m_shortcuts))
	{
		delete shortCut;
	}

	m_shortcuts.clear();

	QSettings defaultSettings(":/Traverso/shortcuts.ini", QSettings::IniFormat);
	QSettings userSettings(QSettings::IniFormat, QSettings::UserScope, "Traverso", "Shortcuts");
	QSettings* settings;

	QStringList defaultGroups = defaultSettings.childGroups();
	QStringList userGroups = userSettings.childGroups();
    QList<TShortCutFunction*> functionsThatInherit;

    for(TShortCutFunction* function : std::as_const(m_functions))
	{
		function->m_keys.clear();
		function->m_modifierkeys.clear();

		if (userGroups.contains(function->commandName))
		{ // prefer user settings over default settings
			settings = &userSettings;
		}
		else if (defaultGroups.contains(function->commandName))
		{ // no user setting available, fallback to default
			settings = &defaultSettings;
		}
		else
		{ // huh ?
			printf("No shortcut definition for function %s\n", QS_C(function->commandName));
			continue;
		}

		settings->beginGroup(function->commandName);
		QString keyString = settings->value("keys").toString();
        QStringList keys = keyString.toUpper().split(";", Qt::SkipEmptyParts);
        QStringList modifiers = settings->value("modifiers").toString().toUpper().split(";", Qt::SkipEmptyParts);
		QString autorepeatinterval = settings->value("autorepeatinterval").toString();
		QString autorepeatstartdelay = settings->value("autorepeatstartdelay").toString();
		QString submenu = settings->value("submenu").toString();
		QString sortorder = settings->value("sortorder").toString();
		if (settings->contains("usesinheritedbase"))
		{
			bool usesInheritedBase = settings->value("usesinheritedbase").toBool();
			if (usesInheritedBase)
			{
                function->set_uses_inherited_base(true);
			}
			else
			{
                function->set_uses_inherited_base(false);
			}
		}
		settings->endGroup();

		function->submenu = submenu;


		foreach(QString string, modifiers)
		{
			int modifier;
            if (keyboard_key_string_to_numerical_value(string, modifier))
			{
				function->m_modifierkeys << modifier;
			}
		}

		bool ok;
		int interval = autorepeatinterval.toInt(&ok);
		if (ok)
		{
            function->set_autorepeat_interval(interval);
		}

		int startdelay = autorepeatstartdelay.toInt(&ok);
		if (ok)
		{
            function->set_autorepeat_start_delay(startdelay);
		}

		int order = sortorder.toInt(&ok);
		if (ok)
		{
			function->sortorder = order;
		}

		function->m_keys << keys;

        if (!function->get_inherited_base().isEmpty())
		{
			functionsThatInherit.append(function);
		}

        if (!function->uses_inherited_base())
		{
            for(const QString &key : function->get_keys())
			{
                TShortCut* shortcut = get_shortcut_for_key(key);
				if (shortcut)
				{
                    shortcut->add_shortcut_function(function);
				}
			}
		}
	}

    for(TShortCutFunction* function : functionsThatInherit)
	{
        TShortCutFunction* inheritedFunction = get_shortcut_function(function->get_inherited_base());
		if (inheritedFunction)
		{
			function->set_inherited_shortcut_function(inheritedFunction);
            if (function->uses_inherited_base())
			{
                for(const QString &key : function->get_keys())
				{
                    TShortCut* shortcut = get_shortcut_for_key(key);
					if (shortcut)
					{
                        shortcut->add_shortcut_function(function);
					}
				}
			}
		}
	}

    emit functionKeysChanged();
}

void TShortCutManager::set_shortcut_function_keys(TShortCutFunction *function, const QStringList& keys, QStringList modifiers)
{
    PENTER;
	function->m_keys.clear();
	function->m_modifierkeys.clear();

	function->m_keys << keys;

	foreach(QString string, modifiers)
	{
		int modifier;
        if (keyboard_key_string_to_numerical_value(string, modifier))
		{
			function->m_modifierkeys << modifier;
		}
	}


    save_shortcut_function(function);
    load_shortcuts();
}

void TShortCutManager::set_shortcut_function_inherited_base(TShortCutFunction *function, bool usesInheritedBase)
{
    PENTER;
    function->set_uses_inherited_base(usesInheritedBase);
    save_shortcut_function(function);
    load_shortcuts();
}

void TShortCutManager::restore_defaults_for_shortcut_function(TShortCutFunction *function)
{
	QSettings userSettings(QSettings::IniFormat, QSettings::UserScope, "Traverso", "Shortcuts");

	userSettings.beginGroup(function->commandName);
	foreach(QString key, userSettings.childKeys())
	{
		userSettings.remove(key);
	}
    load_shortcuts();
}

void TShortCutManager::restore_defaults()
{
	QSettings userSettings(QSettings::IniFormat, QSettings::UserScope, "Traverso", "Shortcuts");
	userSettings.clear();
    load_shortcuts();
}

void TShortCutManager::add_meta_object(const QMetaObject* mo)
{
	QString shortcutItem = QString(mo->className()).remove("View");
	QList<const QMetaObject*> list = m_metaObjects.value(shortcutItem);
	list.append(mo);
	m_metaObjects.insert(shortcutItem, list);

	while(mo)
	{
		QString objectName = mo->className();
		if (objectName == "ContextItem" || objectName == "QObject")
		{
			return;
		}
        register_item_class(shortcutItem, objectName);
		mo = mo->superClass();
	}
}

/**
 * @brief TShortCutManager::register_item_class
 * Used to support multiple inheritance, but not bother the user with multiple
 * object names to dispatch shortcuts to.
 * @param itemName The object name presented to the user in the
 * shortcut help menu or context menu
 * @param className The class that is associated with the object
 * e.g. AudioClip is represented by AudioClipView, AudioClip, TProcessingNode and ViewItem
 *
 */
void TShortCutManager::register_item_class(const QString &itemName, const QString &className)
{
	QStringList classesList = m_classes.value(itemName);
	classesList.append(className);
	m_classes.insert(itemName, classesList);
}

void TShortCutManager::add_translation(const QString &signature, const QString &translation)
{
	m_translations.insert(signature, translation);
}


QString TShortCutManager::get_translation_for(const QString &entry)
{
	QString key = entry;
	key = key.remove("View");
	if (!m_translations.contains(key)) {
        qDebug("TShortCutManager::get_translation_for(%s) not in database", entry.toLatin1().data());
        return QString("TShortCutManager: %1 not found!").arg(key);
	}
	return m_translations.value(key);
}

QString TShortCutManager::create_html_for_class(const QString& className, QObject* object)
{
	QString holdKeyFact = "";

	QString name = get_translation_for(className);

	QString baseColor = QApplication::palette().color(QPalette::Base).name();
	QString alternateBaseColor = QApplication::palette().color(QPalette::AlternateBase).name();

	QString html = QString("<html><head><meta http-equiv=\"content-type\" content=\"text/html; charset=UTF-8\">\n"
	      "<style type=\"text/css\">\n"
	      "table {font-size: 11px;}\n"
	      ".object {background-color: %1; font-size: 11px;}\n"
	      ".description {background-color: %2; font-size: 10px; font-weight: bold;}\n"
	      "</style>\n"
	      "</head>\n<body>\n").arg(QApplication::palette().color(QPalette::AlternateBase).darker(105).name()).arg(alternateBaseColor);

	if (object && object->inherits("PCommand")) {
        PCommand* pc = static_cast<PCommand*>(object);
		html += "<table><tr class=\"object\">\n<td width=220 align=\"center\">" + pc->text() + "</td></tr>\n";
	} else {
		html += "<table><tr class=\"object\">\n<td colspan=\"2\" align=\"center\"><b>" + name + "</b><font style=\"font-size: 11px;\">&nbsp;&nbsp;&nbsp;" + holdKeyFact + "</font></td></tr>\n";
		html += "<tr><td width=110 class=\"description\">" +tr("Description") + "</td><td width=110 class=\"description\">" + tr("Shortcut") + "</td></tr>\n";
	}

	QStringList result;
	int j=0;
    QList<TShortCutFunction* > list = get_shortcut_function_for_class(className);
    QMap<QString, QList<TShortCutFunction*> > functionsMap;

    foreach(TShortCutFunction* function, list)
    {
        QList<TShortCutFunction*> listForKey = functionsMap.value(function->submenu);
		listForKey.append(function);
		functionsMap.insert(function->submenu, listForKey);
	}

	QStringList subMenus = functionsMap.keys();

	foreach(QString submenu, subMenus)
	{
		if (!submenu.isEmpty()) {
			result += "<tr class=\"object\">\n<td colspan=\"2\" align=\"center\">"
				      "<font style=\"font-size: 11px;\"><b>" + submenu + "</b></font></td></tr>\n";
		}

        QList<TShortCutFunction*> subMenuFunctionList = functionsMap.value(submenu);

        foreach(TShortCutFunction* function, subMenuFunctionList)
		{
            QString keySequence = function->get_key_sequence(true);
			keySequence.replace(QString(" , "), QString("<br />"));

			QString alternatingColor;
			if ((j % 2) == 1) {
				alternatingColor = QString("bgcolor=\"%1\"").arg(baseColor);
			} else {
				alternatingColor = QString("bgcolor=\"%1\"").arg(alternateBaseColor);
			}
			j += 1;

            result += QString("<tr %1><td>").arg(alternatingColor) + function->get_description() + "</td><td>" + keySequence + "</td></tr>\n";
		}
	}

	result.removeDuplicates();
	html += result.join("");
	html += "</table>\n";
	html += "</body>\n</html>";

	return html;
}

TCommand * TShortCutManager::export_keymap()
{
	QTextStream out;
	QFile data(QDir::homePath() + "/traversokeymap.html");
	if (data.open(QFile::WriteOnly | QFile::Truncate)) {
		out.setDevice(&data);
	} else {
        return nullptr;
	}

	QString str;
    get_keymap(str);
	out << str;

	data.close();
    return nullptr;
}

TCommand * TShortCutManager::get_keymap(QString &str)
{
	str = "<html><head><meta http-equiv=\"content-type\" content=\"text/html; charset=UTF-8\">\n"
	      "<style type=\"text/css\">\n"
	      "H1 {text-align: left; font-size: 20px;}\n"
	      "table {font-size: 12px; border: solid; border-width: 1px; width: 600px;}\n"
	      ".object {background-color: #ccc; font-size: 16px; font-weight: bold;}\n"
	      ".description {background-color: #ddd; width: 300px; padding: 2px; font-size: 12px; font-weight: bold;}\n"
	      "</style>\n"
	      "</head>\n<body>\n<h1>Traverso keymap: " + config().get_property("InputEventDispatcher", "keymap", "default").toString() + "</h1>\n";

    foreach(QString className, tShortCutManager().get_class_names()) {
        str += tShortCutManager().create_html_for_class(className);
		str += "<p></p><p></p>\n";
	}

	str += "</body>\n</html>";

    return nullptr;
}

bool TShortCutManager::class_inherits(const QString& className, const QString &inherited)
{
	QList<const QMetaObject*> metas = m_metaObjects.value(className);

	foreach(const QMetaObject* mo, metas) {
		while (mo) {
			if (mo->className() == inherited)
			{
				return true;
			}
			mo = mo->superClass();
		}
	}

	return false;
}

bool TShortCutManager::keyboard_key_string_to_numerical_value(const QString &text, int &value)
{
    value = Qt::Key_unknown;

    if (text == "NUMERICAL")
    {
        return true;
    }

    QString s;
    int x  = 0;
    if ((text != "") && (text.length() > 0) ) {
        s="ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        x = s.indexOf(text);
        if (x>=0) {
            value = Qt::Key_A + x;
        } else {
            s="|ESC     |TAB     |BACKTAB |BKSPACE |RETURN  |ENTER   |INSERT  |DELETE  "
                "|PAUSE   |PRINT   |SYSREQ  |CLEAR   ";
            x = s.indexOf("|" + text);
            if (x>=0)
                value = Qt::Key_Escape + (x/9);
            else {
                s="|HOME    |END     |LEFTARROW  |UPARROW  |RIGHTARROW  "
                    "|DOWNARROW  |PRIOR   |NEXT    ";
                x = s.indexOf("|" + text);
                if (x>=0)
                    value = Qt::Key_Home + (x/9);
                else {
                    s="|SHIFT   |CTRL    |META    |ALT     |CAPS    "
                        "|NUMLOCK |SCROLL  ";
                    x = s.indexOf("|" + text);
                    if (x>=0)
                        value = Qt::Key_Shift + (x/9);
                    else {
                        s="F1 F2 F3 F4 F5 F6 F7 F8 F9 F10F11F12";
                        x=s.indexOf(text);
                        if (x>=0) {
                            value = Qt::Key_F1 + (x/3);
                        } else if (text=="SPACE") {
                            value = Qt::Key_Space;
                        } else if (text == "MOUSEBUTTONLEFT") {
                            value = Qt::LeftButton;
                        } else if (text == "MOUSEBUTTONRIGHT") {
                            value = Qt::RightButton;
                        } else if (text == "MOUSEBUTTONMIDDLE") {
                            value = Qt::MiddleButton;
                        } else if (text == "MOUSEBUTTONX1") {
                            value = Qt::XButton1;
                        } else if (text == "MOUSEBUTTONX2") {
                            value = Qt::XButton2;
                        } else if (text == "MOUSESCROLLHORIZONTALLEFT") {
                            value = TShortCutManager::MouseScrollHorizontalLeft;
                        } else if (text =="MOUSESCROLLHORIZONTALRIGHT") {
                            value = TShortCutManager::MouseScrollHorizontalRight;
                        } else if (text == "MOUSESCROLLVERTICALUP") {
                            value = TShortCutManager::MouseScrollVerticalUp;
                        } else if( text == "MOUSESCROLLVERTICALDOWN") {
                            value = TShortCutManager::MouseScrollVerticalDown;
                        } else if( text == "/") {
                            value = Qt::Key_Slash;
                        } else if ( text == "\\") {
                            value = Qt::Key_Backslash;
                        } else if ( text == "[") {
                            value = Qt::Key_BracketLeft;
                        } else if ( text == "]") {
                            value = Qt::Key_BracketRight;
                        } else if ( text == "PAGEUP") {
                            value = Qt::Key_PageUp;
                        } else if ( text == "PAGEDOWN") {
                            value = Qt::Key_PageDown;
                        } else if (text == "MINUS") {
                            value = Qt::Key_Minus;
                        } else if (text == "PLUS") {
                            value = Qt::Key_Plus;
                        } else if (text == ";") {
                            value = Qt::Key_Semicolon;
                        } else if (text == "'") {
                            value = Qt::Key_Apostrophe;
                        } else if (text == ",") {
                            value = Qt::Key_Comma;
                        } else if (text == ".") {
                            value = Qt::Key_Period;
                        } else {
                            printf("KeyStringToValue: No value found for key %s\n", QS_C(text));
                            return false;
                        }
                    }
                }
            }
        }



    }

    // Code found, return true
    return true;
}

