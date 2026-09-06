/*
Copyright (C) 2006-2007 Remon Sijrier

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

#include "TCurveNodeView.h"
#include "TSheetView.h"

#include <QPainter>
#include <QPen>
#include <TCurveNode.h>
#include <TThemer.h>
#include <TCurve.h>
#include "TCurveView.h"

#include <Debugger.h>

TCurveNodeView::TCurveNodeView( TSheetView * sv, TCurveView* curveview, TCurveNode * node, TCurve* guicurve)
    : TViewItem(curveview, nullptr)
    , TCurveNode(guicurve, node->get_when(), node->get_value())
    , m_node(node)
{
	PENTERCONS;
	m_sv = sv;
	m_curveview = curveview;
	m_isSoftSelected = m_isHardSelected = false;

	setFlags(QGraphicsItem::ItemIgnoresTransformations);

    TCurveNodeView::load_theme_data();
    TCurveNodeView::calculate_bounding_rect();

    connect(m_node->m_curve, &TCurve::nodePositionChanged, this, &TCurveNodeView::update_pos);
}

TCurveNodeView::~ TCurveNodeView( )
{
	PENTERDES;
}

void TCurveNodeView::paint( QPainter * painter, const QStyleOptionGraphicsItem * option, QWidget * widget )
{
        // TODO: Render to a pixmap, and just paint that, it should be much faster
	Q_UNUSED(option);
	Q_UNUSED(widget);

    if (m_curveview->item_ignores_context()) {
        return;
    }

	painter->save();

	painter->setRenderHint(QPainter::Antialiasing);

	QColor color = m_color;
    QColor hardSelectOutlineColor(Qt::white);

    if (m_curveview->item_ignores_context()) {
        color.setAlpha(75);
        hardSelectOutlineColor.setAlpha(75);
    }

    if (m_isHardSelected) {
        painter->setPen(hardSelectOutlineColor);
        painter->setBrush(color);
        painter->drawRect(m_boundingRect);
    } else {
        QPainterPath path;
        path.addEllipse(m_boundingRect);
        painter->fillPath(path, color);
    }
	
	painter->restore();
} 


void TCurveNodeView::calculate_bounding_rect()
{
    prepareGeometryChange();

    int size = 8;//themer()->get_property("CurveNode:diameter", 6).toInt();

    if (m_isSoftSelected) {
        m_boundingRect.setWidth(size + 2);
        m_boundingRect.setHeight(m_boundingRect.width());
    } else {
        m_boundingRect.setWidth(size);
        m_boundingRect.setHeight(size);
    }

    update_pos();
}


void TCurveNodeView::set_color(const QColor &color)
{
    m_color = color;
}

void TCurveNodeView::update_pos( )
{
	qreal halfwidth = (m_boundingRect.width() / 2);
	qreal parentheight = m_parentViewItem->get_height();
    qreal when = ((TTimeRef(m_node->get_when()) - m_curveview->get_start_offset()) / m_sv->timeref_scalefactor) - halfwidth;
	qreal value = parentheight - (m_node->get_value() * parentheight + halfwidth);
	setPos(when, value);
		
    set_when_and_value((m_node->get_when() / m_sv->timeref_scalefactor), m_node->get_value());
}

void TCurveNodeView::set_soft_selected(bool selected)
{
	m_isSoftSelected = selected;
    calculate_bounding_rect();
}

void TCurveNodeView::set_hard_selected(bool selected)
{
	m_isHardSelected = selected;
    calculate_bounding_rect();
}

void TCurveNodeView::load_theme_data()
{
	m_color = themer()->get_color("CurveNode:default");
}


