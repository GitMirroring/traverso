/*
Copyright (C) 2005-2006 Remon Sijrier 

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

$Id: TCurveNode.cpp,v 1.8 2007/11/23 14:56:36 r_sijrier Exp $
*/

#include "TCurveNode.h"

#include "qassert.h"
#include <cmath>


TCurveNode::TCurveNode(double when, double value)
{
    m_coeff[0] = m_coeff[1] = m_coeff[2] = m_coeff[3] = 0.0;

    set_when_and_value(when, value);

    next = nullptr;
}

TCurveNode::~TCurveNode()
{

}

void TCurveNode::set_when(double when) {
    Q_ASSERT( ! std::isnan(when));

    m_when = when;

    emit nodePositionChanged();
}

void TCurveNode::set_relative_when_and_value( double relwhen, double value, double range)
{
    Q_ASSERT( ! std::isnan(relwhen));
    Q_ASSERT( ! std::isnan(value));

    m_when = relwhen * range;
    m_value = value;

    emit nodePositionChanged();
}

void TCurveNode::set_when_and_value(double when, double value)
{
    Q_ASSERT( ! std::isnan(when));
    Q_ASSERT( ! std::isnan(value));

    if (qFuzzyCompare(m_when, when) && qFuzzyCompare(m_value, value)) {
        return;
    }
    m_when = when;
    m_value = value;

    emit nodePositionChanged();
}
//eof


