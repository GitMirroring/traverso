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

$Id: CurveNode.h,v 1.9 2007/11/19 11:18:53 r_sijrier Exp $
*/

#ifndef T_CURVENODE_H
#define T_CURVENODE_H

class TCurve;

/**
 * @brief A node of a TCurve: a value at a position in time.
 *
 * Only its curve changes a node, see TCurve::set_node(). The audio thread never
 * reads nodes, it evaluates the TCurveSnapshot the curve publishes.
 */
class TCurveNode
{
public:
    TCurveNode(TCurve* curve, double when, double value);

    double get_when() const {return m_when;}
    double get_value() const {return m_value;}
    TCurve* get_curve() const {return m_curve;}

private:
    TCurve* m_curve;
    double  m_when;
    double  m_value;

    friend class TCurve;
};


#endif
