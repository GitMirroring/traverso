/*
    Copyright (C) 2008 Remon Sijrier 
 
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

#ifndef TAUDIO_CLIP_GROUP_H
#define TAUDIO_CLIP_GROUP_H

#include "TTimeRef.h"

#include <QObject>

class TAudioClip;
class TLocation;

class TAudioClipGroup : QObject
{
    Q_OBJECT

public:
    TAudioClipGroup();
    TAudioClipGroup(QList<TAudioClip*> clips);
	
	void add_clip(TAudioClip* clip);
	void set_clips(QList<TAudioClip*> clips);
    void move_to(int trackIndex, const TTimeRef &location);
	
	void set_snappable(bool snap);
	void set_as_moving(bool move);
    int check_valid_track_index_delta(int delta);
	
	QList<TAudioClip*> copy_clips();
	void add_all_clips_to_tracks();
	void remove_all_clips_from_tracks();
	
	int get_size() const {return m_clips.size();}
	int get_track_index() const {return m_topTrackIndex;}
    TLocation* get_location() const {return m_location;}
	
	bool is_locked() const;
		
private:
	QList<TAudioClip*> m_clips;
    TLocation*        m_location;

	int	m_topTrackIndex;
	int	m_bottomTrackIndex;
	
	void update_state();
};

#endif
