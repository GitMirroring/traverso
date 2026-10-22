/*
Copyright (C) 2005-2006 Remon Sijrier 

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

#pragma once

#include "TCommand.h"
#include "TContextItem.h"
#include <QList>
#include <QStringList>


class TProject;
class TSheet;
class TCommand;
class TResourcesManager;
class QFileSystemWatcher;
class TTransport;
class TIOScheduler;

class TProjectManager : public TContextItem
{
    Q_OBJECT

public:
    TProject* create_new_project(int numSheet, int numTracks, const QString& projectName);
    TProject* create_new_project(const QString& templatefile, const QString& projectName);

    int load_project(const QString& projectName);
    int load_renamed_project(const QString& name);

    bool project_exists(const QString& title);

    int create_projectfilebackup_dir(const QString& rootDir);
    int remove_project(const QString& title);

    void scheduled_for_deletion(TSheet* sheet);
    void delete_sheet(TSheet* sheet);
    void set_current_project_dir(const QString& path);
    void add_valid_project_path(const QString& path);
    void remove_wrong_project_path(const QString& path);

    int rename_project_dir(const QString& olddir, const QString& newdir);
    int restore_project_from_backup(const QString& projectdir, qint64 restoretime);

    QList<qint64> get_backup_date_times(const QString& projectdir);
    QStringList get_projects_list();
    QString get_projects_directory();
    void start_incremental_backup(TProject* project);

    // As exit(), for SIGTERM and SIGHUP: no questions, "ask" on close saves
    void exit_without_questions();

    TProject* get_project();

    TTransport* get_transport() const {return m_transport;}

    // The application's IO schedulers (doc/design/diskio-app-wide.md): one
    // reads and runs the idle work (peak builds), one writes (recordings),
    // each with a pool of its own. Made at the first ask, on the GUI thread;
    // they outlive every project and go with stop_schedulers(), at the end
    // of the application after the last project (never in a static
    // destructor: their threads and pools need Qt).
    TIOScheduler* get_read_scheduler();
    TIOScheduler* get_write_scheduler();
    void stop_schedulers();

    void start(const QString& basepath, const QString& projectname);


public slots:
    TCommand* save_project();
    TCommand* close_current_project();
    TCommand* exit();


private:
    TProjectManager();
    TProjectManager(const TProjectManager&);

    TProject*        m_currentProject;
    TTransport*     m_transport;
    QList<TSheet*>	m_deletionSheetList;
    bool		m_exitInProgress;
    bool		m_closeWithoutQuestions{false};
    QStringList	m_projectDirs;
    QFileSystemWatcher*	m_watcher;
    TIOScheduler*   m_readScheduler{nullptr};
    TIOScheduler*   m_writeScheduler{nullptr};

    static QUndoGroup	m_undogroup;

    void set_current_project(TProject* project);
    void cleanup_backupfiles_for_project(const QString& projectname);
    bool project_is_current(const QString& title);

    // allow this function to create one instance
    friend TProjectManager& pm();

signals:
    void projectLoaded(TProject* );
    void currentProjectDirChanged();
    void projectsListChanged();
    void unsupportedProjectDirChangeDetected();
    void projectDirChangeDetected();
    void projectLoadFailed(QString,QString);
    void projectFileVersionMismatch(QString,QString);

private slots:
    void apply_peak_cache_settings();
    void project_dir_rename_detected(const QString& dirname);
};


// use this function to access the TProjectManager
TProjectManager& pm();

// Use this function to get the loaded Project's Resources Manager
TResourcesManager* resources_manager();

