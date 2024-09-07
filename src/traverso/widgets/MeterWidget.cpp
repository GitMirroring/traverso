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


#include "MeterWidget.h"
#include "Debugger.h"

MeterWidget::MeterWidget(QWidget* parent, MeterView* item)
	: TViewPort(0, parent)
	, m_item(item)
{
	PENTERCONS;
	setMinimumWidth(40);
	setMinimumHeight(10);

	QGraphicsScene* scene = new QGraphicsScene(this);
	setScene(scene);
	
	if (m_item) {
		scene->addItem(m_item);
		m_item->setPos(0,0);
	}

	setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}

MeterWidget::~MeterWidget()
{
}

void MeterWidget::resizeEvent( QResizeEvent *  )
{
	if (m_item) {
		m_item->resize();
	}
}

void MeterWidget::hideEvent(QHideEvent * event)
{
    QWidget::hideEvent(event);
	if (m_item) {
		m_item->hide_event();
	}
}


void MeterWidget::showEvent(QShowEvent * event)
{
    QWidget::showEvent(event);
	if (m_item) {
		m_item->show_event();
	}
}

QSize MeterWidget::minimumSizeHint() const
{
	return QSize(150, 50);
}

QSize MeterWidget::sizeHint() const
{
	return QSize(220, 50);
}






















//eof
 
