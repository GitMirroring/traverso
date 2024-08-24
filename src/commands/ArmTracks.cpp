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

$Id: ArmTracks.cpp,v 1.4 2008/01/21 16:22:11 r_sijrier Exp $
*/

#include "ArmTracks.h"

#include <TAudioTrack.h>
#include <TSheet.h>
#include <TSheetView.h>
#include <TAudioTrackView.h>

#include <TContextPointer.h>

ArmTracks::ArmTracks(TSheetView* view)
    : TMoveCommand(view, nullptr, ("Arm Tracks"))
{
}


ArmTracks::~ArmTracks()
{}

int ArmTracks::prepare_actions()
{
	return -1;
}

int ArmTracks::begin_hold()
{
    return 1;
}

int ArmTracks::finish_hold()
{
	return 1;
}

int ArmTracks::do_action()
{
	return 1;
}

int ArmTracks::undo_action()
{
	return 1;
}

int ArmTracks::jog()
{
    TAudioTrackView* view = d->sv->get_audio_trackview_at_scene_pos(cpointer().scene_pos());
	
	if ( ! view ) {
		return 0;
	}
	
	TAudioTrack* track = view->get_track();
	
	if (! m_tracks.contains(track) ) {
		m_tracks.append(track);
		track->arm();
	}
	
	return 1;
}

// eof

