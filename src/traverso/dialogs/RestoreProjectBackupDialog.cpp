/*
    Copyright (C) 2007-2026 Remon Sijrier
 
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

#include "RestoreProjectBackupDialog.h"
#include "TProjectManager.h"
#include <QTreeWidgetItem>
#include <QDateTime>

#include "TInformUser.h"

RestoreProjectBackupDialog::RestoreProjectBackupDialog(QWidget * parent)
    : QDialog(parent)
{
    setupUi(this);
}


void RestoreProjectBackupDialog::populate_treeview()
{
    dateTreeWidget->clear();

    QLocale currentLocale;

    QString timeFormatWithSeconds = currentLocale.timeFormat(QLocale::ShortFormat);
    if (timeFormatWithSeconds.contains("mm")) {
        timeFormatWithSeconds.replace("mm", "mm:ss");
    } else {
        timeFormatWithSeconds.replace("m", "m:ss");
    }

    QDateTime now = QDateTime::currentDateTime();

    QString currentDateTimeStr = QString("%1 %2").arg(
        currentLocale.toString(now.date(), QLocale::ShortFormat),
        now.time().toString(timeFormatWithSeconds)
        );
    currentDateLable->setText(currentDateTimeStr);

    QList<qint64> list = pm().get_backup_date_times(m_projectname);

    std::sort(list.begin(), list.end(), [](qint64 left, qint64 right) {
        return left > right;
    });

    for(qint64 time : std::as_const(list)) {
        QTreeWidgetItem* item = new QTreeWidgetItem(dateTreeWidget);
        QDateTime datetime = QDateTime::fromMSecsSinceEpoch(time);

        item->setText(0, currentLocale.toString(datetime.date(), QLocale::ShortFormat));
        item->setText(1, datetime.time().toString(timeFormatWithSeconds));

        item->setData(0, Qt::UserRole, time);
    }

    if (!list.isEmpty()) {
        QTreeWidgetItem* item = dateTreeWidget->invisibleRootItem()->child(0);
        dateTreeWidget->setCurrentItem(item);
        lastBackupLable->setText(item->text(0) + " " + item->text(1));
    } else {
        lastBackupLable->setText(tr("No backup(s) available!"));
    }
}

void RestoreProjectBackupDialog::accept()
{
    QTreeWidgetItem* item = dateTreeWidget->currentItem();

    if (!item) {
        reject();
        return;
    }

    qint64 restoretime = item->data(0, Qt::UserRole).toLongLong();
    int sucess = pm().restore_project_from_backup(m_projectname, restoretime);

    if (sucess) {
        pm().load_project(m_projectname);

        QString timeFormatWithSeconds = QLocale::system().timeFormat(QLocale::ShortFormat);
        timeFormatWithSeconds.replace("mm", "mm:ss");

        QDateTime restoredDateTime = QDateTime::fromMSecsSinceEpoch(restoretime);
        QString localizedDateTime = QString("%1 %2").arg(
            QLocale::system().toString(restoredDateTime.date(), QLocale::ShortFormat),
            restoredDateTime.time().toString(timeFormatWithSeconds));

        tInformUser().information(tr("Succesfully restored backup from %1").arg(localizedDateTime));

        hide();
    }
}


void RestoreProjectBackupDialog::reject()
{
    hide();
}


void RestoreProjectBackupDialog::set_project_name(const QString & projectname)
{
    m_projectname = projectname;
    populate_treeview();
}

