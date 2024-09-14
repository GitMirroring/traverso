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
    frp->origX = cpointer().on_first_input_event_x();
    if (m_fadeIn) {
        m_fadeInNewRange = m_fadeInOrigRange = m_fadeIn->get_range();
    }
    if (m_fadeOut) {
        m_fadeOutNewRange = m_fadeOutOrigRange = m_fadeOut->get_range();
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
    if (m_fadeIn) {
        m_fadeIn->set_range( m_fadeInNewRange );
    }
    if (m_fadeOut) {
        m_fadeOut->set_range(m_fadeOutNewRange);
    }
    return 1;
}

int TFadeRangeCommand::undo_action()
{
    if (m_fadeIn) {
        m_fadeIn->set_range( m_fadeInOrigRange );
    }
    if (m_fadeOut) {
        m_fadeOut->set_range(m_fadeOutOrigRange);
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
    m_fadeInNewRange = 1.0;
    m_fadeOutNewRange = 1.0;
    do_action();
}

void TFadeRangeCommand::next_snap_pos()
{
    if (m_fadeIn) {
        TTimeRef snap = frp->sheet->get_snap_list()->next_snap_pos(frp->clip->get_location_start() + m_fadeInNewRange);
        TTimeRef newpos = snap - frp->clip->get_location_start();
        m_fadeInNewRange = newpos.universal_frame();
    }
    if (m_fadeOut) {
        TTimeRef snap = frp->sheet->get_snap_list()->next_snap_pos(frp->clip->get_location_start() + m_fadeOutNewRange) * (m_fadeIn ? -1 : 1);
        TTimeRef newpos = snap - frp->clip->get_location_start();
        m_fadeOutNewRange = newpos.universal_frame();
    }


    do_keyboard_move();
}

void TFadeRangeCommand::prev_snap_pos()
{
    if (m_fadeIn) {
        TTimeRef snap = frp->sheet->get_snap_list()->prev_snap_pos(frp->clip->get_location_start() + m_fadeInNewRange);
        TTimeRef newpos = snap - frp->clip->get_location_start();
        m_fadeInNewRange = newpos.universal_frame();
    }
    if (m_fadeOut) {
        TTimeRef snap = frp->sheet->get_snap_list()->prev_snap_pos(frp->clip->get_location_start() + m_fadeOutNewRange);
        TTimeRef newpos = snap - frp->clip->get_location_start();
        m_fadeOutNewRange = newpos.universal_frame();
    }
    do_keyboard_move();
}

void TFadeRangeCommand::do_keyboard_move()
{
    ied().bypass_jog_until_mouse_movements_exceeded_manhattenlength();

    do_action();

    update_canvas_cursor_text();
}

int TFadeRangeCommand::jog()
{
    int deltaX = frp->origX - (cpointer().mouse_viewport_x());

    if (m_fadeIn) {
        m_fadeInNewRange = m_fadeInOrigRange - ( deltaX * frp->scalefactor);
    }
    if (m_fadeOut) {
        m_fadeOutNewRange = m_fadeOutOrigRange + (deltaX * frp->scalefactor * (m_fadeIn ? -1 : 1));
    }

    do_action();

    update_canvas_cursor_text();

    return 1;
}


void TFadeRangeCommand::update_canvas_cursor_text()
{
    QString location;

    if (m_fadeIn) {
        location = TTimeRef::timeref_to_ms_3(TTimeRef(m_fadeInNewRange));
    }
    if (m_fadeOut) {
        location = TTimeRef::timeref_to_ms_3(TTimeRef(m_fadeOutNewRange));
    }
    if (m_fadeIn && m_fadeOut) {
        location = TTimeRef::timeref_to_ms_3(TTimeRef(m_fadeInNewRange)) + "  |  " + TTimeRef::timeref_to_ms_3(TTimeRef(m_fadeOutNewRange));
    }

    cpointer().set_canvas_cursor_text(location);
}


