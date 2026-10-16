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

#include "TFadeRangeCommand.h"

#include "TFadeCurve.h"
#include "TAudioClip.h"
#include "TSheet.h"
#include "TInputEventDispatcher.h"
#include "TSnapList.h"
#include "TContextPointer.h"
#include "TRtTransaction.h"

TFadeRangeCommand::TFadeRangeCommand(TAudioClip* clip, TFadeCurve* fadeIn, TFadeCurve *fadeOut, qint64 scalefactor)
    : TMoveCommand(nullptr, clip, "")
    , m_fadeIn(fadeIn)
    , m_fadeOut(fadeOut)
    , frp(new FadeRangePrivate())
{
    m_canvasCursorFollowsMouseCursor = false;
    frp->scalefactor = scalefactor;
    frp->clip = clip;
    frp->sheet = clip->get_sheet();
    if (m_fadeIn && m_fadeOut) {
        setText(tr("Fade Both: length"));
    } else if (m_fadeIn) {
        setText(tr("Fade In: length"));
    } else if (m_fadeOut){
        setText(tr("Fade Out: length"));
    }
}

TFadeRangeCommand::~TFadeRangeCommand()
{}

int TFadeRangeCommand::prepare_actions()
{
    return 1;
}

int TFadeRangeCommand::begin_hold()
{
    frp->origX = m_contextPointer->on_first_input_event_x();
    if (m_fadeIn) {
        m_fadeInNewRange = m_fadeInOrigRange = m_fadeIn->get_length();
    }
    if (m_fadeOut) {
        m_fadeOutNewRange = m_fadeOutOrigRange = m_fadeOut->get_length();
    }
    return 1;
}

int TFadeRangeCommand::finish_hold()
{
    delete frp;
    frp = nullptr;
    return 1;
}

int TFadeRangeCommand::do_action()
{
    TRtTransaction transaction;
    if (m_fadeIn) {
        m_fadeIn->set_length(m_fadeInNewRange, transaction);
    }
    if (m_fadeOut) {
        m_fadeOut->set_length(m_fadeOutNewRange, transaction);
    }
    return 1;
}

int TFadeRangeCommand::undo_action()
{
    TRtTransaction transaction;
    if (m_fadeIn) {
        m_fadeIn->set_length(m_fadeInOrigRange, transaction);
    }
    if (m_fadeOut) {
        m_fadeOut->set_length(m_fadeOutOrigRange, transaction);
    }
    return 1;
}

void TFadeRangeCommand::cancel_action()
{
    finish_hold();
    undo_action();
}


void TFadeRangeCommand::move_left()
{

    if (d->doSnap) {
        return prev_snap_pos();
    }

    if (m_fadeIn) {
        m_fadeInNewRange -= frp->scalefactor * d->speed;
    }
    if (m_fadeOut) {
        m_fadeOutNewRange += frp->scalefactor * d->speed * (m_fadeIn ? -1 : 1);
    }

    do_keyboard_move();
}

void TFadeRangeCommand::move_right()
{

    if (d->doSnap) {
        return next_snap_pos();
    }

    if (m_fadeIn) {
        m_fadeInNewRange += frp->scalefactor * d->speed;
    }
    if (m_fadeOut) {
        m_fadeOutNewRange -= frp->scalefactor * d->speed * (m_fadeIn ? -1 : 1);
    }

    do_keyboard_move();
}

void TFadeRangeCommand::reset_length()
{
    m_fadeInNewRange = TTimeRef();
    m_fadeOutNewRange = TTimeRef();
    do_action();
}

void TFadeRangeCommand::next_snap_pos()
{
    if (m_fadeIn) {
        TTimeRef snap = frp->sheet->get_snap_list()->next_snap_pos(frp->clip->get_location_start() + m_fadeInNewRange);
        TTimeRef newpos = snap - frp->clip->get_location_start();
        m_fadeInNewRange = newpos;
    }
    if (m_fadeOut) {
        TTimeRef snap = frp->sheet->get_snap_list()->next_snap_pos(frp->clip->get_location_start() + m_fadeOutNewRange) * (m_fadeIn ? -1 : 1);
        TTimeRef newpos = snap - frp->clip->get_location_start();
        m_fadeOutNewRange = newpos;
    }


    do_keyboard_move();
}

void TFadeRangeCommand::prev_snap_pos()
{
    if (m_fadeIn) {
        TTimeRef snap = frp->sheet->get_snap_list()->prev_snap_pos(frp->clip->get_location_start() + m_fadeInNewRange);
        TTimeRef newpos = snap - frp->clip->get_location_start();
        m_fadeInNewRange = newpos;
    }
    if (m_fadeOut) {
        TTimeRef snap = frp->sheet->get_snap_list()->prev_snap_pos(frp->clip->get_location_start() + m_fadeOutNewRange);
        TTimeRef newpos = snap - frp->clip->get_location_start();
        m_fadeOutNewRange = newpos;
    }
    do_keyboard_move();
}

// A fade is never shorter than 0 or longer than its clip. The command keeps
// its own lengths in range too, so the cursor text matches and dragging back
// past 0 makes the fade grow at once.
void TFadeRangeCommand::clamp_new_ranges()
{
    const TTimeRef clipLength = frp->clip->get_length();

    if (m_fadeInNewRange < TTimeRef()) {
        m_fadeInNewRange = TTimeRef();
    } else if (m_fadeInNewRange > clipLength) {
        m_fadeInNewRange = clipLength;
    }

    if (m_fadeOutNewRange < TTimeRef()) {
        m_fadeOutNewRange = TTimeRef();
    } else if (m_fadeOutNewRange > clipLength) {
        m_fadeOutNewRange = clipLength;
    }
}

void TFadeRangeCommand::do_keyboard_move()
{
    ied().bypass_jog_until_mouse_movements_exceeded_manhattenlength();

    clamp_new_ranges();
    do_action();

    update_canvas_cursor_text();
}

int TFadeRangeCommand::jog()
{
    int deltaX = frp->origX - (m_contextPointer->mouse_viewport_x());

    if (m_fadeIn) {
        m_fadeInNewRange = m_fadeInOrigRange - ( deltaX * frp->scalefactor);
    }
    if (m_fadeOut) {
        m_fadeOutNewRange = m_fadeOutOrigRange + (deltaX * frp->scalefactor * (m_fadeIn ? -1 : 1));
    }

    clamp_new_ranges();
    do_action();

    update_canvas_cursor_text();

    return 1;
}


void TFadeRangeCommand::update_canvas_cursor_text()
{
    QString location;

    if (m_fadeIn) {
        location = TTimeRef::timeref_to_ms_3(m_fadeInNewRange);
    }
    if (m_fadeOut) {
        location = TTimeRef::timeref_to_ms_3(m_fadeOutNewRange);
    }
    if (m_fadeIn && m_fadeOut) {
        location = TTimeRef::timeref_to_ms_3(m_fadeInNewRange) + "  |  " + TTimeRef::timeref_to_ms_3(m_fadeOutNewRange);
    }

    m_contextPointer->set_canvas_cursor_data(QVariant::fromValue(location));
}


