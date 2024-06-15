/*
Copyright (C) 2007 Ben Levitt

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

$Id: LocationItem.cpp,v 1.2 2008/02/21 20:00:48 r_sijrier Exp $
*/

#include "LocationItem.h"
#include "SnapList.h"

#include <Debugger.h>


LocationItem::LocationItem()
{
	m_isSnappable = true;
	snapList = 0;
}

void LocationItem::set_snappable(bool snap)
{
	if (snapList) {
		snapList->mark_dirty();
	}
	m_isSnappable = snap;
}

void LocationItem::set_snap_list(SnapList *sList)
{
	snapList = sList;
}

void LocationItem::set_location_start(const TTimeRef &start)
{
    m_locationStart = start;
}

void LocationItem::set_location_end(const TTimeRef &end)
{
    m_locationEnd = end;
}

bool LocationItem::is_snappable() const
{
	return m_isSnappable;
}

/* EOF */
