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

#ifndef CURVE_NODE_VIEW_H
#define CURVE_NODE_VIEW_H

#include "ViewItem.h"

#include <TCurveNode.h>

class TCurve;
class CurveView;

class CurveNodeView : public ViewItem
{
	Q_OBJECT

public:
    CurveNodeView(TSheetView* sv, CurveView* curveview, TCurveNode* node);
	~CurveNodeView();
	
	void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget);
	void calculate_bounding_rect();
	void set_soft_selected(bool selected);
	void set_hard_selected(bool selected);
	
    void set_color(const QColor& color);
	void load_theme_data();

    inline double get_when() const {return m_guiNode->get_when();}
    inline double get_value() const {return m_guiNode->get_value();}

	TCurveNode* get_curve_node() const {return m_node;}
    TCurveNode* get_gui_curve_node() const {return m_guiNode;}
    CurveView* get_curve_view() const {return m_curveview;}

	bool is_hard_selected() const {return m_isHardSelected;}
	
private:
	CurveView*	m_curveview;
	TCurveNode*	m_node;
    TCurveNode* m_guiNode;
	QColor		m_color;
	bool		m_isSoftSelected;
	bool		m_isHardSelected;

public slots:
	void update_pos();
};

#endif

//eof
 
 
