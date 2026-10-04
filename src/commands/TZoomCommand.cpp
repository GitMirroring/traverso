/*
    Copyright (C) 2005-2026 Remon Sijrier

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
#include "TZoomCommand.h"
#include "TSheetView.h"
#include "TTrackView.h"
#include "TSheet.h"
#include "TTrack.h"
#include "TClipsViewPort.h"
#include "TContextPointer.h"
#include "TInputEventDispatcher.h"
#include "Debugger.h"
#include <cmath>

TZoomCommand::TZoomCommand(TSheetView* sv, const QVariantList& args)
    : TCommand("Zoom")
    , m_sv(sv)
{
    m_tv = sv->get_trackview_at_scene_pos(cpointer().scene_pos());
    m_canvasCursorFollowsMouseCursor = false;
    m_jogHorizontal = m_jogVertical = false;

    if (!args.empty()) {
        QString type = args.at(0).toString();
        if (type == "JogZoom") {
            m_jogHorizontal = m_jogVertical = true;
        } else if (type == "HJogZoom") {
            m_jogHorizontal = true;
        } else if (type == "VJogZoom") {
            m_jogVertical = true;
        }
    }

    if (args.size() > 1) {
        m_xScalefactor = args.at(1).toDouble();
    } else {
        m_xScalefactor = 1.0;
    }

    if (args.size() > 2) {
        m_yScalefactor = args.at(2).toDouble();
    } else {
        m_yScalefactor = 0.0;
    }

    m_trackHeight = collected_number_to_track_height(ied().get_collected_number());
}

int TZoomCommand::prepare_actions()
{
    return 1;
}

int TZoomCommand::begin_hold()
{
    m_verticalJogZoomLastY = m_contextPointer->mouse_viewport_y();
    m_horizontalJogZoomLastX = m_contextPointer->mouse_viewport_x();
    m_origPos = m_contextPointer->scene_pos();

    return 1;
}

int TZoomCommand::finish_hold()
{
    QCursor::setPos(m_mousePos);
    return -1;
}

void TZoomCommand::set_cursor_shape(int useX, int useY)
{
    if (useX && useY) {
        m_contextPointer->set_canvas_cursor_type(TContextPointer::CursorType::ZoomHorizontal);
    } else if (useX) {
        m_contextPointer->set_canvas_cursor_type(TContextPointer::CursorType::ZoomHorizontal);
    } else if (useY) {
        m_contextPointer->set_canvas_cursor_type(TContextPointer::CursorType::ZoomVertical);
    }

    m_mousePos = QCursor::pos();

    if (useY && m_tv) {
        int height = m_sv->get_track_height(m_tv->get_track());
        m_contextPointer->set_canvas_cursor_data(QVariant::fromValue(QString("%1 px").arg(height)));
    } else {
        m_contextPointer->set_canvas_cursor_data(QVariant::fromValue(QStringLiteral("Zooming")));
    }
}

int TZoomCommand::jog()
{
    PENTER;

    static const int VERTICAL_JOG_THRESHOLD_PIXELS = 8;
    static const int HORIZONTAL_JOG_THRESHOLD_PIXELS = 10;

    if (m_jogVertical && m_tv) {
        int y = m_contextPointer->mouse_viewport_y();
        int dy = y - m_verticalJogZoomLastY;

        if (std::abs(dy) > VERTICAL_JOG_THRESHOLD_PIXELS) {
            m_verticalJogZoomLastY = y;
            if (dy > 0) {
                m_sv->vzoom(1.0 + m_yScalefactor);
            } else {
                m_sv->vzoom(1.0 - m_yScalefactor);
            }

            int currentHeight = m_sv->get_track_height(m_tv->get_track());
            m_contextPointer->set_canvas_cursor_data(QVariant::fromValue(QString("%1 px").arg(currentHeight)));
        }
    }

    if (m_jogHorizontal) {
        int x = m_contextPointer->mouse_viewport_x();
        int dx = x - m_horizontalJogZoomLastX;

        if (std::abs(dx) > HORIZONTAL_JOG_THRESHOLD_PIXELS) {
            m_horizontalJogZoomLastX = x;
            if (dx > 0) {
                hzoom_in();
            } else {
                hzoom_out();
            }

            m_contextPointer->set_canvas_cursor_data(QVariant::fromValue(QStringLiteral("Scaling")));
        }
    }

    m_contextPointer->set_canvas_cursor_pos(m_origPos);
    return 1;
}


int TZoomCommand::do_action()
{
    if (m_yScalefactor != 0.0) {
        m_sv->vzoom(1.0 + m_yScalefactor);
    }
    if (m_xScalefactor != 1.0) {
        m_sv->hzoom(m_xScalefactor);
    }

    return -1;
}

int TZoomCommand::undo_action()
{
    return -1;
}

void TZoomCommand::vzoom_in()
{
    m_sv->vzoom(1.3);
}

void TZoomCommand::vzoom_out()
{
    m_sv->vzoom(0.7);
}

void TZoomCommand::hzoom_in()
{
    m_sv->hzoom(0.5);
}

void TZoomCommand::hzoom_out()
{
    m_sv->hzoom(2.0);
}

void TZoomCommand::track_vzoom_in()
{
    if (!m_tv) {
        return;
    }

    int trackheight = m_sv->get_track_height(m_tv->get_track());
    trackheight = static_cast<int>(trackheight * 1.3);

    m_sv->set_track_height(m_tv, trackheight);
}

void TZoomCommand::track_vzoom_out()
{
    if (!m_tv) {
        return;
    }

    int trackheight = m_sv->get_track_height(m_tv->get_track());
    trackheight = static_cast<int>(trackheight * 0.7);

    m_sv->set_track_height(m_tv, trackheight);
}

void TZoomCommand::process_collected_number(const QString &collected)
{
    if (!m_tv || collected.isEmpty()) {
        return;
    }

    int newHeight = collected_number_to_track_height(collected);

    // An output height specification of -1 signals a fall-through request for full viewport allocation
    if (newHeight == -1) {
        m_sv->set_track_height(m_tv, m_sv->get_clips_viewport()->height());
    } else {
        m_sv->set_track_height(m_tv, newHeight);
    }
}

int TZoomCommand::collected_number_to_track_height(const QString& collected) const
{
    int number = 0;
    int trackHeight = TTrack::INITIAL_HEIGHT;
    bool ok = false;

    QString cleared = collected;
    cleared = cleared.remove(".").remove("-").remove(",");

    if (!cleared.isEmpty()) {
        number = QString(cleared.data()[cleared.size() - 1]).toInt(&ok);
    } else {
        return -1;
    }

    if (ok && m_tv) {
        switch(number) {
        case 2:  trackHeight = 60;  break;
        case 3:  trackHeight = 100; break;
        case 4:  trackHeight = 180; break;
        case 5:  trackHeight = 320; break;
        case 6:  trackHeight = 640; break;
        case 7:  trackHeight = -1;  break; // Standard placeholder token mapping out full height allocation
        default: trackHeight = 40;
        }
    }

    return trackHeight;
}

void TZoomCommand::toggle_vertical_horizontal_jog_zoom()
{
    if (m_jogVertical) {
        m_contextPointer->set_canvas_cursor_type(TContextPointer::CursorType::ZoomHorizontal);
        m_contextPointer->set_canvas_cursor_data(QVariant::fromValue(QStringLiteral("Horizontal")));
        m_contextPointer->set_canvas_cursor_pos(m_origPos);
        m_jogVertical = false;
        m_jogHorizontal = true;
    } else {
        m_contextPointer->set_canvas_cursor_type(TContextPointer::CursorType::ZoomVertical);
        if (m_tv) {
            int height = m_sv->get_track_height(m_tv->get_track());
            m_contextPointer->set_canvas_cursor_data(QVariant::fromValue(QString("%1 px").arg(height)));
        }
        m_contextPointer->set_canvas_cursor_pos(m_origPos);
        m_jogVertical = true;
        m_jogHorizontal = false;
    }
}


void TZoomCommand::toggle_expand_all_tracks()
{
    m_sv->toggle_expand_all_tracks(-1);
}

void TZoomCommand::numerical_input()
{
    m_contextPointer->set_canvas_cursor_text(tr("Use numerical input to set track height.."), 2000);
}
