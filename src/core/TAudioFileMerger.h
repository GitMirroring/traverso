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

#ifndef TAUDIO_FILE_MERGER_H
#define TAUDIO_FILE_MERGER_H

#include <QThread>
#include <QQueue>
#include <QMutex>

class TBufferedAudioStreamReader;

class TAudioFileMerger : public QThread
{
	Q_OBJECT
public:
    TAudioFileMerger();
	void run() {
		exec();
	}
	
	void enqueue_task(TBufferedAudioStreamReader* source0, TBufferedAudioStreamReader* source2, const QString& dir, const QString& outfilename);
	void stop_merging();

		
private slots:
	void dequeue_tasks();
	
private:
	struct MergeTask {
		QString outFileName;
		QString dir;
		TBufferedAudioStreamReader* readsource0;
		TBufferedAudioStreamReader* readsource1;
	};
	
	QQueue<MergeTask> m_tasks;
	QMutex m_mutex;
	bool m_stopMerging;
	
	void process_task(MergeTask task);
	
signals:
	void dequeueTask();
	void progress(int);
	void taskStarted(QString);
	void taskFinished(QString);
	void processingStopped();
};

#endif
