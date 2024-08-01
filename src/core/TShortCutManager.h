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

#ifndef TSHORTCUTMANAGER_H
#define TSHORTCUTMANAGER_H

#include <QObject>
#include <QStringList>
#include <QHash>
#include <QMap>

class TCommandPlugin;
class TCommand;
class TShortCut;
class TShortCutFunction;


class TShortCutManager : public QObject
{
	Q_OBJECT
public:

    static const int MouseScrollHorizontalLeft = -1;
    static const int MouseScrollHorizontalRight = -2;
    static const int MouseScrollVerticalUp = -3;
    static const int MouseScrollVerticalDown = -4;


    void createAndAddFunction(const QString &object, const QString &description, const QString &slotSignature, const QString &commandName, const QString& inheritedBase = "");
    void registerFunction(TShortCutFunction* function);
    TShortCutFunction* getFunction(const QString& function) const;

    QList<TShortCutFunction* > getFunctionsFor(QString className);
    TShortCut* getShortcutForKey(const QString& key);
    TShortCut* getShortcutForKey(int key);
    TCommandPlugin* getCommandPlugin(const QString& pluginName);
    void modifyFunctionKeys(TShortCutFunction* function, const QStringList& keys, QStringList modifiers);
    void modifyFunctionInheritedBase(TShortCutFunction* function, bool usesInheritedBase);
	void add_translation(const QString& signature, const QString& translation);
	void add_meta_object(const QMetaObject* mo);
	void registerItemClass(const QString& item, const QString& className);
    void register_command_plugin(TCommandPlugin* plugin, const QString& pluginName);
	QString get_translation_for(const QString& entry);
    QString createHtmlForClass(const QString& className, QObject* obj=nullptr);
	QList<QString> getClassNames() const;
	QString getClassForObject(const QString& object) const;
	bool classInherits(const QString& className, const QString &inherited);

	void loadFunctions();
    void saveFunction(TShortCutFunction* function);
    void saveFunctions(QList<TShortCutFunction*> functions);
	void exportFunctions();
	void loadShortcuts();
    void restoreDefaultFor(TShortCutFunction* function);
	void restoreDefaults();

	bool isCommandClass(const QString& className);

private:
    QHash<QString, TCommandPlugin*>	m_commandPlugins;

    QHash<QString, TShortCutFunction*>	m_functions;
    QHash<int, TShortCut*>		m_shortcuts;
	QHash<QString, QString>		m_translations;
	QHash<QString, QList<const QMetaObject*> > m_metaObjects;

    // be sure to only insert into m_classes using registerItemClass()
    // to avoid overwriting existing entries
    QMap<QString, QStringList>	m_classes;

    TShortCutManager();
    void extracted();
    ~TShortCutManager();
    TShortCutManager(const TShortCutManager&) : QObject() {}

    friend TShortCutManager& tShortCutManager();

    bool keyboard_key_string_to_numerical_value(const QString& text, int& value);

public slots:
	TCommand* export_keymap();
	TCommand* get_keymap(QString &);

signals:
	void functionKeysChanged();
};

TShortCutManager& tShortCutManager();


#endif // TSHORTCUTMANAGER_H
