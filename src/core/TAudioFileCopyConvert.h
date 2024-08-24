/*
Copyright (C) 2007 Remon Sijrier 

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

#ifndef TAUDIO_FILE_COPY_CONVERT_H
#define TAUDIO_FILE_COPY_CONVERT_H

#include <QThread>
#include <QQueue>
#include <QMutex>

class TReadAudioSource;
class TExportSpecification;

class TAudioFileCopyConvert : public QThread
{
	Q_OBJECT
public:
    TAudioFileCopyConvert();
	void run() {
		exec();
	}
	
	void enqueue_task(TReadAudioSource* source, TExportSpecification* spec, const QString& dir, const QString& outfilename, int tracknumber, const QString& trackname);
	void stop_merging();

		
private slots:
	void dequeue_tasks();
	
private:
	struct CopyTask {
		QString outFileName;
		QString dir;
		QString extension;
		int tracknumber;
		QString trackname;
		TReadAudioSource* readsource;
		TExportSpecification* spec;
	};
	
	QQueue<CopyTask> m_tasks;
	QMutex m_mutex;
	bool m_stopProcessing;
	
	void process_task(CopyTask task);
	
signals:
	void dequeueTask();
	void progress(int);
	void taskStarted(QString);
	void taskFinished(QString, int, QString);
	void processingStopped();
};

#endif
