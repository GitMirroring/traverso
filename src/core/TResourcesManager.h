/*
Copyright (C) 2006-2007 Remon Sijrier 

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

#ifndef RESOURCES_MANAGER_H
#define RESOURCES_MANAGER_H

#include "TTimeRef.h"
#include <QString>
#include <QHash>
#include <QList>
#include <QDomDocument>
#include <QObject>


class TBufferedAudioStream;
class TBufferedAudioStreamReader;
class TAudioClip;
class TProject;

class TResourcesManager : public QObject
{
	Q_OBJECT
	
public:
    TResourcesManager(TProject* project);
    ~TResourcesManager();

	int set_state( const QDomNode& node );
	QDomNode get_state(QDomDocument doc);
	
	TBufferedAudioStreamReader* create_recording_source(const QString& dir,
				const QString& name,
				uint channelCount,
				qint64 sheetId);
	
	TBufferedAudioStreamReader* import_source(const QString& dir, const QString& name);
	TBufferedAudioStreamReader* get_silent_readsource();
	TAudioClip* new_audio_clip(const QString& name);
	TAudioClip* get_clip(qint64 id);

	void mark_clip_removed(TAudioClip* clip);
	void mark_clip_added(TAudioClip* clip);
    void set_source_for_clip(TAudioClip* clip, TBufferedAudioStreamReader *source);
	void destroy_clip(TAudioClip* clip);
	void remove_source(TBufferedAudioStreamReader* source);
	
	bool is_clip_in_use(qint64) const;
	bool is_source_in_use(qint64 id) const;

	TBufferedAudioStreamReader* get_readsource(qint64 id);
	
	QList<TBufferedAudioStreamReader*> get_all_audio_sources() const;
	QList<TAudioClip*> get_all_clips() const;


private:
	struct ClipData {
		ClipData();
		TAudioClip* clip;
		bool inUse;
		bool isCopy;
		bool removed;
	};
	
	struct SourceData {
		SourceData();
		TBufferedAudioStreamReader* stream;
		int clipCount;
	};
	
	TProject* m_project;
	QHash<qint64, SourceData* >	m_sources;
	QHash<qint64, ClipData* >	m_clips;
	TBufferedAudioStreamReader*			m_silentReadSource;
	
	
signals:
	void stateRestored();
	void clipRemoved(TAudioClip* clip);
	void clipAdded(TAudioClip* clip);
	void sourceAdded(TBufferedAudioStreamReader* source);
	void sourceRemoved(TBufferedAudioStreamReader* source);
};



#endif

