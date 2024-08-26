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

#ifndef METER_WIDGET_H
#define METER_WIDGET_H

#include "MeterView.h"

#include <QTimer>

#include <ViewPort.h>
#include <ViewItem.h>

class MeterView;
class TSession;
class TProject;
class TAudioPlugin;

class MeterWidget : public ViewPort
{

public:
	MeterWidget(QWidget* parent, MeterView* item);
	~MeterWidget();

protected:
	void resizeEvent( QResizeEvent* e);
	void hideEvent ( QHideEvent * event );
	void showEvent ( QShowEvent * event );
	QSize minimumSizeHint () const;
	QSize sizeHint () const;
	MeterView* m_item;
};



#endif

 
