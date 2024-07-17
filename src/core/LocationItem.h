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

$Id: Snappable.h,v 1.1 2007/03/29 22:18:38 benjie Exp $
*/

#ifndef SNAPPABLE_H
#define SNAPPABLE_H

#include "TTimeRef.h"

class SnapList;


class LocationItem
{
public:
    LocationItem();
    ~LocationItem() {}

	void set_snappable(bool snap);

	bool is_snappable() const;

	void set_snap_list(SnapList *sList);

    inline TTimeRef get_location_start() const {return m_locationStart;}
    inline TTimeRef get_location_end() const {return m_locationEnd;}
    TTimeRef get_length() const {return m_locationEnd - m_locationStart;}


    void set_location_start(const TTimeRef& start);
    void set_location_end(const TTimeRef& end);

protected:
    TTimeRef     m_locationStart;
    TTimeRef     m_locationEnd;

private:
	bool		m_isSnappable;
	SnapList	*snapList;

};


#endif

/* EOF */
