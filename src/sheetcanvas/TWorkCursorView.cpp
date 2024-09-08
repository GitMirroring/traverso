/*
    Copyright (C) 2005-2007 Remon Sijrier 
 
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

#include "TWorkCursorView.h"
#include "TSheetView.h"
#include <TSheet.h>
#include <TThemer.h>
#include "Debugger.h"

#include <QPen>
#include <QScrollBar>
#include <QApplication>

TWorkCursorView::TWorkCursorView(TSheetView* sv, TSession* session)
        : TViewItem(nullptr, session)
        , m_session(session)
	, m_sv(sv)
{
	setZValue(100);
}

TWorkCursorView::~TWorkCursorView( )
{
        PENTERDES2;
}

void TWorkCursorView::paint( QPainter * painter, const QStyleOptionGraphicsItem * option, QWidget * widget )
{
	Q_UNUSED(option);
	Q_UNUSED(widget);
	
	if (m_pix.height() != int(m_boundingRect.height())) {
		update_background();
	}
	
	painter->drawPixmap(0, 0, int(m_boundingRect.width()), int(m_boundingRect.height()), m_pix);
}

void TWorkCursorView::update_position()
{
	setPos(m_session->get_work_location() / m_sv->timeref_scalefactor, 1);
}

void TWorkCursorView::set_bounding_rect( QRectF rect )
{
	m_boundingRect = rect;
}

void TWorkCursorView::update_background()
{
	m_pix = QPixmap(int(m_boundingRect.width()), int(m_boundingRect.height()));
	m_pix.fill(Qt::transparent);
	QPainter p(&m_pix);
	QPen pen;
	pen.setWidth(4);
	pen.setStyle(Qt::DashDotLine);
	pen.setColor(themer()->get_color("Workcursor:default"));
	p.setPen(pen);
	p.drawLine(0, 0, int(m_boundingRect.width()), int(m_boundingRect.height()));
}
