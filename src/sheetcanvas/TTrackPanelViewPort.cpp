/*
    Copyright (C) 2006 Remon Sijrier 
 
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
 
    $Id: TTrackPanelViewPort.cpp,v 1.1 2008/01/21 16:17:30 r_sijrier Exp $
*/

#include "TTrackPanelViewPort.h"
		
#include "TSheetWidget.h"
#include "TSheetView.h"
#include "TTrackPanelView.h"
#include <TContextPointer.h>
#include <QWheelEvent>
#include "TInputEventDispatcher.h"

#include <Debugger.h>


TTrackPanelViewPort::TTrackPanelViewPort(QGraphicsScene* scene, TSheetWidget* sw)
	: TViewPort(scene, sw)
{
        setFixedWidth(230);

	m_sw = sw;
        m_sv = sw->get_sheetview();
	
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}

void TTrackPanelViewPort::wheelEvent (QWheelEvent *event)
{
  	if (event->angleDelta().x() > 0) {
  		m_sv->scroll_left_by(event->angleDelta().x());
  	} else if (event->angleDelta().x() < 0) {
  		m_sv->scroll_right_by(-event->angleDelta().x());
  	}
    if (event->angleDelta().y() > 0) {
  		m_sv->scroll_up_by(event->angleDelta().y());
  	} else if (event->angleDelta().y() < 0) {
  		m_sv->scroll_down_by(-event->angleDelta().y());
  	}
}

// Catch native trackpad gestures
bool TTrackPanelViewPort::event(QEvent *event)
{
    if (event->type() == QEvent::NativeGesture) {
        QNativeGestureEvent *gestureEvent = static_cast<QNativeGestureEvent*>(event);
        if (gestureEvent->gestureType() == Qt::ZoomNativeGesture) {
            qreal zoomFactor = 2*gestureEvent->value(); 

            if (ied().is_holding_modifier_key(Qt::Key_Shift)) {
                m_sv->vzoom(1+zoomFactor);
            }
            else {
                m_sv->hzoom(1-zoomFactor);
            }
            
            return true; // Event handled
        }
    }
    return QWidget::event(event);
}

//eof
