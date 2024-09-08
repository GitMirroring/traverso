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

#include "TShortcutEditorDialog.h"
#include "TShortCut.h"
#include "TShortCutFunction.h"
#include "traverso_autogen/include/ui_TShortcutEditorDialog.h"
#include "ui_TShortcutEditorDialog.h"

#include "TShortCutManager.h"
#include <QTreeWidgetItem>

#include "Debugger.h"

TShortcutEditorDialog::TShortcutEditorDialog(QWidget *parent)
	: QDialog(parent),
	ui(new Ui::TShortcutEditorDialog)
{
	ui->setupUi(this);

	ui->upPushButton->hide();
	ui->downPushButton->hide();
#ifdef USE_DEBUGGER
	ui->upPushButton->show();
	ui->downPushButton->show();
#endif

	resize(780, 400);

	QStringList keys;
	keys << "|";

	for (int i=65; i<=90; ++i)
	{
		QString string = QChar(i);
		string = string + "|" + string;
		keys << string;
	}
	for (int i=1; i<=12; ++i)
	{
		QString string("F%1");
		string = string.arg(i);
		string = QString(string + "|" + string);
		keys << string;
    }
    keys << "Left|LEFTARROW" << "Right|RIGHTARROW" << "Up|UPARROW" << "Down|DOWNARROW";
    keys << "Enter|ENTER" << "Home|HOME" << "End|END" << "Delete|DELETE" << "Backspace|BKSPACE";
	keys << "Page Up|PAGEUP" << "Page Down|PAGEDOWN";
	keys << "Space Bar|SPACE";
	keys << "+|PLUS" << "-|MINUS" << "/|/" << "\\|\\" << "[|[" << "]|]" << ",|," << ".|." << ";|;" << "'|'";

	foreach(QString string, keys)
	{
		QStringList list = string.split("|");
        ui->keyComboBox1->addItem(list.at(0), list.at(1));
	}
    keys.clear();
    keys << "|";
    keys << "Left Button|MOUSEBUTTONLEFT" << "Right Button|MOUSEBUTTONRIGHT";
    keys << "Scroll Up|MOUSESCROLLVERTICALUP" << "Scroll Down|MOUSESCROLLVERTICALDOWN";
    foreach(QString string, keys)
    {
        QStringList list = string.split("|");
        ui->keyComboBox2->addItem(list.at(0), list.at(1));
    }


    QMap<QString, QString> classNamesMap;
    QMap<QString, QString> baseClassNamesMap;
    QMap<QString, QString> commandClassNamesMap;

    foreach(QString className, tShortCutManager().get_class_names()) {
        if (tShortCutManager().is_command_class(className)) {
            commandClassNamesMap.insert(tShortCutManager().get_translation_for(className), className);
		}
        else if (className.contains("Base")) {
            baseClassNamesMap.insert(tShortCutManager().get_translation_for(className), className);
        } else {
            classNamesMap.insert(tShortCutManager().get_translation_for(className), className);
		}
	}

	foreach(QString className, classNamesMap.values()) {
		ui->objectsComboBox->addItem(classNamesMap.key(className), className);
	}

    foreach(QString className, baseClassNamesMap)
    {
        ui->objectsComboBox->addItem(baseClassNamesMap.key(className) + " " + tr("(Base Shortcut)"), className);
    }

    foreach(QString className, commandClassNamesMap)
    {
        ui->objectsComboBox->addItem(commandClassNamesMap.key(className) + " " + tr("(Function keys)"), className);
    }

	connect(ui->objectsComboBox, SIGNAL(activated(int)), this, SLOT(objects_combo_box_activated(int)));
	connect(ui->shortcutsTreeWidget, SIGNAL(itemSelectionChanged()), this, SLOT(shortcut_tree_widget_item_activated()));
	connect(ui->showfunctionsCheckBox, SIGNAL(clicked()), this, SLOT(show_functions_checkbox_clicked()));
	connect(&tShortCutManager(), SIGNAL(functionKeysChanged()), this, SLOT(function_keys_changed()));
	connect(ui->keyComboBox1, SIGNAL(activated(int)), this, SLOT(key1_combo_box_activated(int)));
	connect(ui->keyComboBox1, SIGNAL(activated(int)), this, SLOT(key_combo_box_activated(int)));
	connect(ui->keyComboBox2, SIGNAL(activated(int)), this, SLOT(key_combo_box_activated(int)));
	connect(ui->altCheckBox, SIGNAL(clicked()), this, SLOT(modifier_combo_box_toggled()));
	connect(ui->ctrlCheckBox, SIGNAL(clicked()), this, SLOT(modifier_combo_box_toggled()));
	connect(ui->shiftCheckBox, SIGNAL(clicked()), this, SLOT(modifier_combo_box_toggled()));
	connect(ui->metaCheckBox, SIGNAL(clicked()), this, SLOT(modifier_combo_box_toggled()));
	connect(ui->startDelaySpinBox, SIGNAL(editingFinished()), this, SLOT(modifier_combo_box_toggled()));
	connect(ui->repeatIntervalSpinBox, SIGNAL(editingFinished()), this, SLOT(modifier_combo_box_toggled()));
	connect(ui->configureInheritedShortcutPushButton, SIGNAL(clicked()), this, SLOT(configure_inherited_shortcut_pushbutton_clicked()));
	connect(ui->baseFunctionGroupBox, SIGNAL(clicked()), this, SLOT(base_function_checkbox_clicked()));
	connect(ui->buttonBox, SIGNAL(clicked(QAbstractButton*)), this, SLOT(button_box_button_clicked(QAbstractButton*)));

	// teasing the dialog to get into the 'no functions selected' state
	// and updating it accordingly.
	show_functions_checkbox_clicked();
}

TShortcutEditorDialog::~TShortcutEditorDialog()
{
	delete ui;
}

void TShortcutEditorDialog::objects_combo_box_activated(int index)
{
	ui->shortcutsTreeWidget->clear();

	QString className = ui->objectsComboBox->itemData(index).toString();

    if (tShortCutManager().is_command_class(className))
	{
		ui->shortCutGroupBox->setTitle(tr("Modifier &Key"));
		ui->shortcutsTreeWidget->setHeaderLabels(QStringList() << tr("Function") << tr("Modifier Key"));
	}
	else {
        ui->shortCutGroupBox->setTitle(tr("&Key  / Button"));
        ui->shortcutsTreeWidget->setHeaderLabels(QStringList() << tr("Function") << tr("Key / Button"));
	}

    QList<TShortCutFunction* > functionsList = tShortCutManager().get_shortcut_functions_for_class(className);

	foreach(TShortCutFunction* function, functionsList)
	{
		QTreeWidgetItem* item;
		item = new QTreeWidgetItem(QStringList() << function->get_long_description() << function->get_key_sequence());
        QVariant v = QVariant::fromValue((void*) function);
		item->setData(0, Qt::UserRole, v);
		ui->shortcutsTreeWidget->addTopLevelItem(item);
	}

	QTreeWidgetItem* item = ui->shortcutsTreeWidget->topLevelItem(0);
	ui->shortcutsTreeWidget->setCurrentItem(item);
}

void TShortcutEditorDialog::modifier_combo_box_toggled()
{
	key_combo_box_activated(0);
}

void TShortcutEditorDialog::key_combo_box_activated(int)
{
	if (ui->showfunctionsCheckBox->isChecked()) {
		return;
	}

	TShortCutFunction* function = getSelectedFunction();
	if (!function)
	{
		return;
	}

	QStringList modifiers;

	if (ui->altCheckBox->isChecked()) modifiers << "ALT";
	if (ui->ctrlCheckBox->isChecked()) modifiers << "CTRL";
	if (ui->shiftCheckBox->isChecked()) modifiers << "SHIFT";
	if (ui->metaCheckBox->isChecked()) modifiers << "META";

	QStringList keys;

	QString key1 = ui->keyComboBox1->itemData(ui->keyComboBox1->currentIndex()).toString();
	if (!key1.isEmpty())
	{
		keys << key1;
	}

	QString key2 = ui->keyComboBox2->itemData(ui->keyComboBox2->currentIndex()).toString();
	if (!key2.isEmpty())
	{
		keys << key2;
	}

	if (function->uses_autorepeat())
	{
		function->set_autorepeat_interval(ui->repeatIntervalSpinBox->value());
		function->set_autorepeat_start_delay(ui->startDelaySpinBox->value());
	}

    tShortCutManager().set_shortcut_function_keys(function, keys, modifiers);
}

void TShortcutEditorDialog::key1_combo_box_activated(int /*index*/)
{
	if (!ui->showfunctionsCheckBox->isChecked())
	{
		return;
	}

	ui->shortcutsTreeWidget->clear();

	QString keyString = ui->keyComboBox1->itemData(ui->keyComboBox1->currentIndex()).toString();
    TShortCut* shortCut = tShortCutManager().get_shortcut_for_key(keyString);

	if (!shortCut)
	{
		return;
	}

    for(TShortCutFunction* function : shortCut->get_shortcut_functions())
	{
        QString translatedObjectName = tShortCutManager().get_translation_for(function->get_metaobject()->className());
		QStringList stringlist;
		stringlist << translatedObjectName << function->get_long_description() << function->get_key_sequence();
		QTreeWidgetItem* item = new QTreeWidgetItem(stringlist);
        QVariant v = QVariant::fromValue((void*) function);
		item->setData(0, Qt::UserRole, v);

        ui->shortcutsTreeWidget->addTopLevelItem(item);
	}
    ui->shortcutsTreeWidget->sortItems(0, Qt::SortOrder::AscendingOrder);
}

TShortCutFunction* TShortcutEditorDialog::getSelectedFunction()
{
	QList<QTreeWidgetItem*> items = ui->shortcutsTreeWidget->selectedItems();

	if (!items.size())
	{
		return 0;

	}
	QTreeWidgetItem *item = items.first();

	return (TShortCutFunction*) item->data(0, Qt::UserRole).value<void*>();
}

void TShortcutEditorDialog::shortcut_tree_widget_item_activated()
{
	if (ui->showfunctionsCheckBox->isChecked())
	{
		TShortCutFunction* function = getSelectedFunction();
		if (function)
		{
            int index = ui->objectsComboBox->findData(tShortCutManager().get_class_for_object(function->get_metaobject()));
			if (index >= 0)
			{
				ui->objectsComboBox->setCurrentIndex(index);
				ui->showfunctionsCheckBox->setChecked(false);
				show_functions_checkbox_clicked();
				QList<QTreeWidgetItem*> items = ui->shortcutsTreeWidget->findItems(function->get_long_description(), Qt::MatchCaseSensitive);
				if (items.size())
				{
					ui->shortcutsTreeWidget->setCurrentItem(items.first());
				}
			}
		}

		return;
	}

	ui->keyComboBox1->setCurrentIndex(0);
	ui->keyComboBox2->setCurrentIndex(0);
	ui->shiftCheckBox->setChecked(false);
	ui->ctrlCheckBox->setChecked(false);
	ui->altCheckBox->setChecked(false);
	ui->metaCheckBox->setChecked(false);

	TShortCutFunction* function = getSelectedFunction();
	if (!function)
	{
        ui->baseFunctionGroupBox->hide();
		ui->autorepeatGroupBox->hide();
		ui->shortCutGroupBox->setEnabled(false);
		ui->modifiersGroupBox->setEnabled(false);
		return;
	}

	bool isHoldFunction = false;
	int index = ui->objectsComboBox->currentIndex();
	if (index >=0)
	{
		QString objectClassName = ui->objectsComboBox->itemData(index).toString();
		isHoldFunction = tShortCutManager().class_inherits(objectClassName, "TCommand");
	}

	QStringList keys = function->get_keys(false);
	if (keys.size() > 0)
	{
		QString keySequence = keys.at(0);
        TShortCutFunction::make_shortcut_key_human_readable(keySequence);
		int index = ui->keyComboBox1->findText(keySequence, Qt::MatchFixedString);
		ui->keyComboBox1->setCurrentIndex(index);
	}
	if (keys.size() > 1)
	{
		QString keySequence = keys.at(1);
        TShortCutFunction::make_shortcut_key_human_readable(keySequence);
		int index = ui->keyComboBox2->findText(keySequence, Qt::MatchFixedString);
		ui->keyComboBox2->setCurrentIndex(index);
	}

	TShortCutFunction* inheritedFunction = function->get_base_shortcut_function();
	bool usesInheritedBase = function->uses_inherited_base();
	if (inheritedFunction)
	{
        ui->baseFunctionShortCutKey->setText(inheritedFunction->get_key_sequence());
        ui->baseFunctionShortCutLable->setText(inheritedFunction->get_description());
		ui->configureInheritedShortcutPushButton->setText(tr("Configure %1").arg(inheritedFunction->get_description()));
        ui->baseFunctionGroupBox->show();

		if (usesInheritedBase)
		{
			ui->baseFunctionGroupBox->setChecked(true);
		}
		else
		{
			ui->baseFunctionGroupBox->setChecked(false);
		}
	}
	else
	{
        ui->baseFunctionGroupBox->hide();
	}

	if (isHoldFunction)
	{
		ui->modifiersGroupBox->setEnabled(false);
	}
	else
	{
		ui->autorepeatGroupBox->hide();

		QList<int> modifierKeys = function->get_modifier_keys(false);
		if (modifierKeys.contains(Qt::Key_Shift)) {
			ui->shiftCheckBox->setChecked(true);
		}
		if (modifierKeys.contains(Qt::Key_Control)) {
			ui->ctrlCheckBox->setChecked(true);
		}
		if (modifierKeys.contains(Qt::Key_Alt))	{
			ui->altCheckBox->setChecked(true);
		}
		if (modifierKeys.contains(Qt::Key_Meta)) {
			ui->metaCheckBox->setChecked(true);
		}
	}

	if (isHoldFunction)
	{
		ui->shortCutGroupBox->setEnabled(true);
		ui->modifiersGroupBox->setEnabled(false);
	}
	else if (usesInheritedBase)
	{
		ui->shortCutGroupBox->setEnabled(false);
		ui->modifiersGroupBox->setEnabled(false);
	}
	else
	{
		ui->shortCutGroupBox->setEnabled(true);
		ui->modifiersGroupBox->setEnabled(true);
	}

	if (function->uses_autorepeat())
	{
		ui->autorepeatGroupBox->show();
		ui->startDelaySpinBox->setValue(function->get_autorepeat_start_delay());
		ui->repeatIntervalSpinBox->setValue(function->get_autorepeat_interval());
	}
	else
	{
		ui->autorepeatGroupBox->hide();
	}
}

void TShortcutEditorDialog::base_function_checkbox_clicked()
{
	TShortCutFunction* function = getSelectedFunction();
	if (!function)
	{
		return;
	}


	if (ui->baseFunctionGroupBox->isChecked())
	{
        tShortCutManager().set_shortcut_function_uses_base_function(function, true);
	}
	else
	{
        tShortCutManager().set_shortcut_function_uses_base_function(function, false);
	}
}

void TShortcutEditorDialog::show_functions_checkbox_clicked()
{
	if (ui->showfunctionsCheckBox->isChecked())
	{
		ui->shortcutsTreeWidget->setColumnCount(3);
		ui->shortcutsTreeWidget->setHeaderLabels(QStringList() << tr("Item") << tr("Function") << tr("Shortcut"));
		ui->shortcutsTreeWidget->header()->resizeSection(0, 190);
		ui->shortcutsTreeWidget->header()->resizeSection(1, 190);
		ui->objectsComboBox->hide();
		ui->keyComboBox2->hide();
        ui->baseFunctionGroupBox->hide();
		key1_combo_box_activated(ui->keyComboBox1->currentIndex());
        ui->shortCutGroupBox->setTitle(tr("&Key  / Button"));
        ui->shortCutGroupBox->setEnabled(true);
		ui->modifiersGroupBox->setEnabled(false);
	}
	else
	{
		ui->objectsComboBox->show();
		ui->modifiersGroupBox->show();
		ui->shortCutGroupBox->setEnabled(true);
		ui->modifiersGroupBox->setEnabled(true);
		ui->keyComboBox2->show();
		ui->shortcutsTreeWidget->setColumnCount(2);
		ui->shortcutsTreeWidget->header()->resizeSection(0, 280);
		objects_combo_box_activated(ui->objectsComboBox->currentIndex());
	}
}

void TShortcutEditorDialog::function_keys_changed()
{
	QWidget* fWidget = focusWidget();

	QTreeWidgetItem* item = ui->shortcutsTreeWidget->currentItem();
	if (!item) {
		return;
	}

	TShortCutFunction* function = (TShortCutFunction*) item->data(0, Qt::UserRole).value<void*>();

	objects_combo_box_activated(ui->objectsComboBox->currentIndex());

	// item is now deleted!

	QList<QTreeWidgetItem*> items = ui->shortcutsTreeWidget->findItems(function->get_long_description(), Qt::MatchCaseSensitive);
	if (items.size())
	{
		ui->shortcutsTreeWidget->setCurrentItem(items.first());
	}

	fWidget->setFocus();
}

void TShortcutEditorDialog::configure_inherited_shortcut_pushbutton_clicked()
{
	TShortCutFunction* function = getSelectedFunction();
	if (!function)
	{
		return;
	}
	function = function->get_base_shortcut_function();
	if (!function)
	{
		return;
	}

    int index = ui->objectsComboBox->findData(function->get_metaobject()->className());
	if (index >= 0)
	{
		ui->objectsComboBox->setCurrentIndex(index);
		objects_combo_box_activated(index);
	}
}

void TShortcutEditorDialog::on_restoreDefaultPushButton_clicked()
{
	TShortCutFunction* function = getSelectedFunction();
	if (!function)
	{
		return;
	}

    tShortCutManager().restore_defaults_for_shortcut_function(function);
}

void TShortcutEditorDialog::button_box_button_clicked(QAbstractButton* button)
{
	if (button == ui->buttonBox->button(QDialogButtonBox::RestoreDefaults))
	{
        tShortCutManager().restore_defaults();

	}
}

void TShortcutEditorDialog::on_downPushButton_clicked()
{
	moveItemUpDown(1);
}

void TShortcutEditorDialog::on_upPushButton_clicked()
{
	moveItemUpDown(-1);
}

void TShortcutEditorDialog::moveItemUpDown(int direction)
{
    PENTER;
	QTreeWidgetItem* item = ui->shortcutsTreeWidget->currentItem();
	if (!item)
	{
		return;
	}

	int index = ui->shortcutsTreeWidget->indexOfTopLevelItem(item);
	if ((index + direction) < 0 || (index + direction) >= ui->shortcutsTreeWidget->topLevelItemCount())
	{
		return;
	}
	ui->shortcutsTreeWidget->takeTopLevelItem(index);
	ui->shortcutsTreeWidget->insertTopLevelItem(index + direction, item);
	ui->shortcutsTreeWidget->setCurrentItem(item);

	for (int i=0; i<ui->shortcutsTreeWidget->topLevelItemCount(); ++i)
	{
		item = ui->shortcutsTreeWidget->topLevelItem(i);
		TShortCutFunction* function = (TShortCutFunction*) item->data(0, Qt::UserRole).value<void*>();
        function->set_sort_order(i);
	}

    tShortCutManager().export_functions();
}

void TShortcutEditorDialog::changeEvent(QEvent *e)
{
	QDialog::changeEvent(e);
	switch (e->type()) {
	case QEvent::LanguageChange:
		ui->retranslateUi(this);
		break;
	default:
		break;
	}
}

