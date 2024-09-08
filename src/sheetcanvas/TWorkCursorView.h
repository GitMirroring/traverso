/*
    Copyright (C) 2005-2024 Remon Sijrier
 
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

#ifndef TWORK_CURSOR_VIEW_H
#define TWORK_CURSOR_VIEW_H

#include "TPlayHeadView.h"
#include "TViewItem.h"
#include <QTimer>
#include <QTimeLine>
#include <QBrush>

class TSession;
class TSheetView;
class TClipsViewPort;
		

class TWorkCursorView : public TViewItem
{
        Q_OBJECT

public:
        TWorkCursorView(TSheetView* sv, TSession* session);
        ~TWorkCursorView();

	void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget);
	void set_bounding_rect(QRectF rect);

private:
        TSession*	m_session;
	TSheetView*	m_sv;
	QPixmap		m_pix;
	
	void update_background();

public slots:
        void update_position();
};


#endif

//eof
