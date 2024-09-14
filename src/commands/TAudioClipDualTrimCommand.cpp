/*
    Copyright (C) 2024 Remon Sijrier

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

#include "TAudioClipDualTrimCommand.h"
#include "TAudioClip.h"
#include "TAudioTrack.h"

#include "TContextPointer.h"
#include "TSheetView.h"

TAudioClipDualTrimCommand::TAudioClipDualTrimCommand(TSheetView* sv, TAudioTrack *audioTrack)
    : TMoveCommand(sv, audioTrack, tr("AudioClip Dual Trim"))
    , m_audioTrack(audioTrack)
{

}

int TAudioClipDualTrimCommand::prepare_actions()
{
    return 1;
}

int TAudioClipDualTrimCommand::do_action()
{
    m_leftAudioClip->set_right_edge(m_newLocation);
    m_rightAudioClip->set_left_edge(m_newLocation);

    return 1;
}

int TAudioClipDualTrimCommand::undo_action()
{
    m_leftAudioClip->set_right_edge(m_origLocation);
    m_rightAudioClip->set_left_edge(m_origLocation);

    return 1;
}

int TAudioClipDualTrimCommand::begin_hold()
{
    TTimeRef cursorLocation = cpointer().on_first_input_event_timeref_location();

    TAudioClip* pointedAudioClip = m_audioTrack->get_clip_at_location(cursorLocation);
    if (!pointedAudioClip) {
        set_canvas_cursor_text(tr("Dual Trim: No AudioClip at this location"));
        return -1;
    }

    TAudioClip* audioClipBefore = m_audioTrack->get_audio_clip_before(pointedAudioClip);
    TAudioClip* audioClipAfter = m_audioTrack->get_audio_clip_after(pointedAudioClip);

    bool leftEdgeCanTrim = false;
    bool rightEdgeCanTrim = false;
    bool trimLeftEdge = false;
    bool trimRightEdge = false;
    if (audioClipBefore && audioClipBefore->get_location_end() == pointedAudioClip->get_location_start()) {
        leftEdgeCanTrim = true;
    }
    if (audioClipAfter && audioClipAfter->get_location_start() == pointedAudioClip->get_location_end()) {
        rightEdgeCanTrim = true;
    }

    if (leftEdgeCanTrim && rightEdgeCanTrim) {
        TTimeRef leftEdgeDistance = cursorLocation - pointedAudioClip->get_location_start();
        TTimeRef rightEdgeDistance = pointedAudioClip->get_location_end() - cursorLocation;
        if (leftEdgeDistance < rightEdgeDistance) {
            trimLeftEdge = true;
        } else {
            trimRightEdge = true;
        }
    } else if (leftEdgeCanTrim) {
        trimLeftEdge = true;
    } else if (rightEdgeCanTrim){
        trimRightEdge = true;
    }

    if (! (trimLeftEdge || trimRightEdge)) {
        set_canvas_cursor_text(tr("Dual Trim: No AudioClip before/after this one"));
        return -1;
    }

    if (trimLeftEdge) {
        m_leftAudioClip = audioClipBefore;
        m_rightAudioClip = pointedAudioClip;
    }
    if (trimRightEdge) {
        m_leftAudioClip = pointedAudioClip;
        m_rightAudioClip = audioClipAfter;
    }

    m_origLocation = m_newLocation = m_leftAudioClip->get_location_end();


    return 1;
}

int TAudioClipDualTrimCommand::finish_hold()
{
    return 1;
}

void TAudioClipDualTrimCommand::cancel_action()
{
    undo_action();
}

int TAudioClipDualTrimCommand::jog()
{
    m_newLocation = cpointer().timeref_location();

    cpointer().set_canvas_cursor_pos(cpointer().on_first_input_event_scene_pos());

    do_action();

    return 1;

}

void TAudioClipDualTrimCommand::set_canvas_cursor_text(const QString &text)
{
    cpointer().set_canvas_cursor_text(text, 2000);
}
