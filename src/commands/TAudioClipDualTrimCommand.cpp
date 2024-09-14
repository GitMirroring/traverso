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
    TTimeRef timeRef = cpointer().on_first_input_event_timeref_location();

    m_leftAudioClip = m_audioTrack->get_clip_at_location(timeRef);
    if (!m_leftAudioClip) {
        set_canvas_cursor_text(tr("Dual Trim: No AudioClip at this location"));
        return -1;
    }
    m_rightAudioClip = m_audioTrack->get_audio_clip_after(m_leftAudioClip);
    if (!m_rightAudioClip) {
        set_canvas_cursor_text(tr("Dual Trim: No AudioClip after this one"));
        return -1;
    }
    if (m_leftAudioClip->get_location()->get_end() != m_rightAudioClip->get_location()->get_start()) {
        printf("%lld, %lld\n", m_leftAudioClip->get_location()->get_end().universal_frame(), m_rightAudioClip->get_location()->get_start().universal_frame());
        set_canvas_cursor_text(tr("Dual Trim: Gap between first and second AudioClip"));
        return -1;
    }

    m_origLocation = m_newLocation = m_leftAudioClip->get_location()->get_end();

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

    do_action();

    return 1;

}

void TAudioClipDualTrimCommand::set_canvas_cursor_text(const QString &text)
{
    cpointer().set_canvas_cursor_text(text, 2000);
}
