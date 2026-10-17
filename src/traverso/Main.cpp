/*
Copyright (C) 2005-2009 Remon Sijrier 

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

#include <cstdio>
#include <csignal>
#include <cstdlib>
#include <unistd.h>

#include <QFile>
#include <QLocale>
#include <QTextStream>
#include <QTranslator>

#include "TConfig.h"
#include "Traverso.h"
#include "TProject.h"
#include "TProjectManager.h"
#include "TMainWindow.h"
#include "TShortCutManager.h"
#include "Debugger.h"
#include "TLog.h"

Traverso* traverso;

int signalcount = 0;


#if defined (Q_OS_LINUX) || defined (Q_OS_MAC)
void catch_signal(int sig_num)
{
	if (!signalcount) {
		++signalcount;
		traverso->shutdown(sig_num);
		exit(0);
	} else {
                printf("Caught multiple signals, aborting !\n");
		_exit(-1);
	}
}
#endif


int main( int argc, char **argv )
{
#if defined (Q_OS_LINUX) || defined (Q_OS_MAC)
	// Only SIGINT: a crash must stay a crash (core dump, debugger stop)
	signal(SIGINT, catch_signal);
#endif

	// The session log is always written; stderr too in developer builds
	QString logDirectory = TLog::default_directory();
#ifdef TRAVERSO_DEV_LOG
	bool logToStderr = true;
#else
	bool logToStderr = false;
#endif
	if (argc > 1) {
		for (int i=1; i<argc; i++) {
			if (strncmp(argv[i], "--log-level=", 12) == 0) {
				TLogLevel level;
				if (TLog::level_from_name(argv[i] + 12, level)) {
					TLog::set_level(level);
				} else {
					printf("Unknown log level '%s', use error, warning, info, debug or trace\n", argv[i] + 12);
					return 1;
				}
			}
			if (strcmp(argv[i],"--log-stderr")==0)
					logToStderr = true;
			if (strcmp(argv[i],"--no-log-stderr")==0)
					logToStderr = false;
			if (strncmp(argv[i], "--log-dir=", 10) == 0)
					logDirectory = QString::fromLocal8Bit(argv[i] + 10);
			if ((strcmp(argv[i],"--help")==0) || (strcmp(argv[i],"-h")==0)) {
				printf("\nUsage: traverso [OPTIONS]\n\n");
				printf("Valid OPTIONS are :\n\n");
				printf("\t--help or -h \t This help text\n");
				printf("\t -v   \t\t Print version number\n");
				printf("\t--log-level=LEVEL\t error, warning, info (default), debug or trace (the last two in developer builds)\n");
				printf("\t--log-stderr / --no-log-stderr\t Also write the log to stderr (default in developer builds)\n");
				printf("\t--log-dir=DIR\t\t Directory of traverso.log (default: ~/.local/state/Traverso, ~/Library/Logs/Traverso or AppData/Local/Traverso/logs)\n");
				printf("\t--show-compile-options\t\t Print options used during compilation\n");
                                printf("\t--fft-meter   \t\t Start Traverso as a Spectral Analyzer\n");
                                printf("\t--dump-shortcut-table FILE\t Write the shortcut resolution table to FILE and quit\n");
                                printf("\n");
				return 0;
			}
			if (strcmp(argv[i],"-v")==0) {
                printf("Traverso %s\n", VERSION);
				return 0;
			}
			if (strcmp(argv[i],"--show-compile-options")==0) {
                // printf("Traverso compile options: %s\n", TRAVERSO_DEFINES);
				return 0;
			}
		}
	}
	// Before the application: Qt's messages while it starts go to the log too
	TLog::name_thread("gui");
	TLog::start(logDirectory, logToStderr, VERSION);
	TLOG_I("app", "log file {}", TLog::file_name());

	traverso = new Traverso(argc, argv);

	// Writes the shortcut resolution table for both built-in keymaps and quits,
	// used to verify that shortcut handling keeps behaving the same
	for (int i=1; i<argc-1; i++) {
		if (strcmp(argv[i],"--dump-shortcut-table")==0) {
			QFile file(QString::fromLocal8Bit(argv[i+1]));
			if (!file.open(QFile::WriteOnly | QFile::Truncate)) {
				return 1;
			}
			QTextStream out(&file);
			QVariant simplified = config().get_property("InputEventDispatcher", "UseSimplifiedShortcuts", true);
			for (bool useSimplified : {true, false}) {
				config().set_property("InputEventDispatcher", "UseSimplifiedShortcuts", useSimplified);
				tShortCutManager().load_shortcuts();
				out << "## KEYMAP " << (useSimplified ? "simplified" : "advanced") << "\n";
				tShortCutManager().write_resolution_table(out);
			}
			config().set_property("InputEventDispatcher", "UseSimplifiedShortcuts", simplified);
			return 0;
		}
	}
	
    QTranslator traversoTranslator;
	QString systemLanguage = QLocale::system().name();
	QString userLanguage = config().get_property("Interface", "LanguageFile", "").toString();
    bool translaterLoaded = false;
	if (userLanguage.isEmpty() || userLanguage.isNull()) {
        translaterLoaded = traversoTranslator.load(":/translations/traverso_" + systemLanguage );
	} else {
        translaterLoaded = traversoTranslator.load(userLanguage);
	}

    if (translaterLoaded) {
        traverso->installTranslator(&traversoTranslator);
    }

        traverso->create_interface();

        if (argc > 1) {
                for (int i=1; i<argc; i++) {
                        if (strcmp(argv[i],"--fft-meter")==0) {
                                printf("Using Traverso in FFT Meter only mode\n");
                                if (!pm().project_exists("fft-meter")) {
                                        TProject* project = pm().create_new_project(1, 1, "fft-meter");
                                        project->save();
                                        delete project;

                                }
                                pm().load_project("fft-meter");
                                TMainWindow::instance()->show_fft_meter_only();
                        }
                }
        }


	traverso->exec();
	
	delete traverso;

	TLOG_I("app", "Thank you for using Traverso !");
	TLog::stop();
	return 0;
}

