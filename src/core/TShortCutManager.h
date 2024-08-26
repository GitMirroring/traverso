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


    void register_shortcut_function(TShortCutFunction* function);
    TShortCutFunction* get_shortcut_function(const QString& function) const;

    QList<TShortCutFunction* > get_shortcut_function_for_class(QString className);
    TShortCut* get_shortcut_for_key(const QString& key);
    TShortCut* get_shortcut_for_key(int key);
    TCommandPlugin* get_command_plugin(const QString& pluginName);

    void set_shortcut_function_keys(TShortCutFunction* function, const QStringList& keys, QStringList modifiers);
    void set_shortcut_function_inherited_base(TShortCutFunction* function, bool usesInheritedBase);
	void add_translation(const QString& signature, const QString& translation);
	void add_meta_object(const QMetaObject* mo);
    void register_item_class(const QString& item, const QString& className);
    void register_command_plugin(TCommandPlugin* plugin, const QString& pluginName);

    QString get_translation_for(const QString& entry);
    QString create_html_for_class(const QString& className, QObject* obj=nullptr);
    QList<QString> get_class_names() const;
    QString get_class_for_object(const QMetaObject *metaObject) const;

    bool class_inherits(const QString& className, const QString &inherited);

    void save_shortcut_function(TShortCutFunction* function);
    void save_shortcut_fuctions(QList<TShortCutFunction*> functions);
    void export_functions();
    void load_shortcuts();
    void restore_defaults_for_shortcut_function(TShortCutFunction* function);
    void restore_defaults();

    bool is_command_class(const QString& className);

private:
    QHash<QString, TCommandPlugin*>	m_commandPlugins;

    QHash<QString, TShortCutFunction*>	m_functions;
    QHash<int, TShortCut*>		m_shortcuts;
	QHash<QString, QString>		m_translations;
	QHash<QString, QList<const QMetaObject*> > m_metaObjects;

    // be sure to only insert into m_classes using register_item_class()
    // to avoid overwriting existing entries
    QMap<QString, QStringList>	m_classes;

    TShortCutManager();
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
