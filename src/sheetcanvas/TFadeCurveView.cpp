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

$Id: FadeCurveView.cpp,v 1.2 2008/05/24 17:27:49 r_sijrier Exp $
*/

#include "TFadeCurveView.h"

#include <QPainter>

#include "TCurveNode.h"
#include "TFadeCurve.h"
#include "TAudioClipView.h"
#include "TSheetView.h"
#include "TMainWindow.h"
#include <TThemer.h>
#include <Fade.h>
#include "TInputEventDispatcher.h"
#include <TAddRemoveCommand.h>

#include <TSheet.h>
#include <Utils.h>

#include <Debugger.h>

static const int DOT_SIZE		= 6;
static const QString DOT_COLOR		= "#78817B";

TFadeCurveView::TFadeCurveView(TSheetView* sv, TAudioClipView* parent, TFadeCurve * fadeCurve )
    : ViewItem(parent, fadeCurve)
    , m_fadeCurve(fadeCurve)
{
	PENTERCONS;
    m_sv = sv;
    m_audioClip = parent->get_clip();
	m_holdactive = false;
	m_guicurve = new TCurve(nullptr);
	m_guicurve->set_sheet(m_sv->get_sheet());

    Q_ASSERT(m_fadeCurve);

    for(TCurveNode* node = m_fadeCurve->get_nodes().first(); node != nullptr; node = node->next) {
		TCurveNode* guinode = new TCurveNode(m_guicurve, 
				node->get_when() / m_sv->timeref_scalefactor,
				node->get_value());
        TAddRemoveCommand* cmd = qobject_cast<TAddRemoveCommand*>(m_guicurve->add_node(guinode, false));
		cmd->set_instantanious(true);
		TCommand::process_command(cmd);
	}

    TFadeCurveView::load_theme_data();

    setFlags(QGraphicsItem::ItemUsesExtendedStyleOption);

	connect(m_fadeCurve, SIGNAL(stateChanged()), this, SLOT(state_changed()));
	connect(m_fadeCurve, SIGNAL(rangeChanged()), this, SLOT(state_changed()));
}


TFadeCurveView::~ TFadeCurveView( )
{
	PENTERDES;
	delete m_guicurve;
}


void TFadeCurveView::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
	Q_UNUSED(widget);
	
	
    int pixelcount = int(option->exposedRect.width());
	
	if (pixelcount == 0) {
		return;
	}

	QPolygonF polygon;
    qreal xstart = option->exposedRect.x();
    if (xstart > 0) {
            xstart -= 1;
            pixelcount += 2;
    }
    qreal vector_start = xstart;
    qreal height = m_boundingRect.height();
    TAudioBuffer buffer(pixelcount);

	if (m_fadeCurve->get_fade_type() == TFadeCurve::FadeOut && m_guicurve->get_range() > m_parentViewItem->boundingRect().width()) {
        vector_start += m_guicurve->get_range() - m_parentViewItem->boundingRect().width();
	}
	
    m_guicurve->get_vector(vector_start, vector_start + pixelcount, buffer, nframes_t(pixelcount));
	
	for (int i=0; i<pixelcount; i++) {
        polygon <<  QPointF(xstart + i, height - (double(buffer[i]) * height) );
	}
	
	
	painter->save();
    painter->setClipRect(m_boundingRect.intersected(m_parentViewItem->boundingRect()));
    painter->setRenderHint(QPainter::Antialiasing);
	
	QPainterPath path;
	
	path.addPolygon(polygon);
	path.lineTo(xstart + 1 + pixelcount, 0);
	path.lineTo(xstart + 1, 0);
	path.closeSubpath();
	
	painter->setPen(Qt::NoPen);
	
	QColor color = m_fadeCurve->is_bypassed() ? 
			themer()->get_color("Fade:bypassed") :
			themer()->get_color("Fade:default");
	
        if (has_active_context()) {
		color.setAlpha(color.alpha() + 10);
	}
	
	painter->setBrush(color);
	painter->drawPath(path);	


	if (m_holdactive) {
		// Calculate and draw control points
        qreal h = m_boundingRect.height() - 1;
        qreal w = m_boundingRect.width() - 1;
		QList<QPointF> points = m_fadeCurve->get_control_points();
        QPointF p1((points.at(1).x() * w + 0.5), h - (points.at(1).y() * h + 0.5));
        QPointF p2(w - ((1.0 - points.at(2).x()) * w + 0.5), ((1.0 - points.at(2).y()) * h + 0.5));
	
		painter->setPen(QColor(DOT_COLOR));
		painter->setBrush(QColor(DOT_COLOR));
		
		if (m_fadeCurve->get_fade_type() == TFadeCurve::FadeOut) {
			p1.setX(w - int((1 - points.at(2).x()) * w + 0.5));
			p1.setY(h - int((1 - points.at(2).y()) * h + 0.5));
			p2.setX(int((points.at(1).x()) * w + 0.5));
			p2.setY(int((points.at(1).y()) * h + 0.5));
            painter->drawLine(QPointF(w, h), QPointF(p1.x(), p1.y()));
            painter->drawLine(QPointF(0, 0), QPointF(p2.x(), p2.y()));
		} else {
            painter->drawLine(QPointF(0, h), QPointF(p1.x(), p1.y()));
            painter->drawLine(QPointF(w, 0), QPointF(p2.x(), p2.y()));
		}
		
        painter->drawEllipse(QPointF(p1.x() - DOT_SIZE/2, p1.y() - DOT_SIZE/2), DOT_SIZE, DOT_SIZE);
        painter->drawEllipse(QPointF(p2.x() - DOT_SIZE/2, p2.y() - DOT_SIZE/2), DOT_SIZE, DOT_SIZE);
	}
	
	painter->restore();
}

int TFadeCurveView::get_vector(qreal xstart, int pixelcount, TAudioBuffer &buffer)
{
	// If boundingrect width is smaller then a pixel, don't even try
	if (m_boundingRect.width() < 1.0) {
		return 0;
	}
	
	if (m_fadeCurve->get_fade_type() == TFadeCurve::FadeOut) {
		
		// If the fade widt is larger the the clipview, add the difference,
		// since the 'start' of the FadeCurveView lies beyond the left edge of the clip!
		if (m_boundingRect.width() > m_parentViewItem->boundingRect().width()) {
            xstart += m_boundingRect.width() - m_parentViewItem->boundingRect().width();
		}
		
		// map the xstart position to the FadeCurveViews x position
        qreal mappedx = mapFromParent(QPointF(xstart, 0)).x();
        qreal x = mappedx;
        // float* p = buffer;
		
		// check if the xstart lies before 'our' first pixel
		if (mappedx < 0) {
			x = 0;
			// substract the difference from the pixelcount
			pixelcount += mappedx;
			
			// point to the mapped location of the buffer.
            // p = buffer - int(mappedx);
            buffer.set_data_start_offset(-mappedx);
			
			// and if pixelcount is 0, there is nothing to do!
			if (pixelcount <= 0) {
				return 0;
			}
			
			// Any pixels outside of our range shouldn't alter the waveform,
			// so let's assign 1 to them!
			for (int i=0; i < - mappedx; ++i) {
                buffer[i] = 1;
			}
		}

        m_guicurve->get_vector(x, x + pixelcount, buffer, nframes_t(pixelcount));

        buffer.set_data_start_offset(0);
		
		return 1;
	}
	
	if (xstart < m_boundingRect.width()) {
        m_guicurve->get_vector(xstart, xstart + pixelcount, buffer, nframes_t(pixelcount));
		return 1;
	}
	
	return 0;
}

void TFadeCurveView::calculate_bounding_rect()
{
    prepareGeometryChange();

    TRealTimeLinkedList<TCurveNode*> guinodes = m_guicurve->get_nodes();
    TRealTimeLinkedList<TCurveNode*> nodes = m_fadeCurve->get_nodes();
	
    TCurveNode* node = nodes.first();
    TCurveNode* guinode = guinodes.first();
	
	while (node) {
        guinode->set_when_and_value(node->get_when() / m_sv->timeref_scalefactor, node->get_value());
		
		node = node->next;
		guinode = guinode->next;
	}
	
	double range = m_guicurve->get_range();
	m_boundingRect = QRectF( 0, 0, range, m_parentViewItem->get_height() );
	
	if (m_fadeCurve->get_fade_type() == TFadeCurve::FadeOut) {
        qreal diff = 0;
		if (m_boundingRect.width() > m_parentViewItem->boundingRect().width()) {
            diff = m_boundingRect.width() - m_parentViewItem->boundingRect().width();
		}
		setPos(m_parentViewItem->boundingRect().width() - m_boundingRect.width() + diff, 0);
	} else {
		setPos(0, 0);
	}
}


void TFadeCurveView::state_changed( )
{
    PENTER;
	prepareGeometryChange();
	calculate_bounding_rect();
	update();
	
	emit fadeModified();
}


TCommand* TFadeCurveView::bend()
{
	return new FadeBend(this);
}

TCommand* TFadeCurveView::strength()
{
	return new FadeStrength(this);
}

TCommand* TFadeCurveView::select_fade_shape()
{
    if (m_fadeCurve->get_fade_type() == TFadeCurve::FadeIn) {
		TMainWindow::instance()->select_fade_in_shape();
	}
	else {
		TMainWindow::instance()->select_fade_out_shape();
	}
	return nullptr;
}

void TFadeCurveView::set_holding(bool hold)
{
	m_holdactive = hold;
	update(m_boundingRect);
}


void TFadeCurveView::load_theme_data()
{
    TFadeCurveView::calculate_bounding_rect();
}

