/*
Copyright (C) 2006-2024 Nicola Doebelin, Remon Sijrier

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

#include "SnapList.h"

#include "TSession.h"
#include "Sheet.h"
#include "AudioClip.h"
#include "AudioClipManager.h"
#include "TConfig.h"
#include "TTimeLineRuler.h"
#include "Utils.h"
#include "Marker.h"

#include <QString>

#include <Debugger.h>

// #define debugsnaplist

#if defined(debugsnaplist)
#define SLPRINT(args...) printf(args)
#else
#define SLPRINT(args...);
#endif

SnapList::SnapList(TSession* session)
    : m_session(session)
{
	m_isDirty = true;
    m_wasDirty = false;
	m_rangeStart = TTimeRef();
	m_rangeEnd = TTimeRef();
	m_scalefactor = 1;
}

void SnapList::mark_dirty()
{
// 	printf("mark_dirty()\n");
	m_isDirty = true;
	m_wasDirty = true;
}

void SnapList::update_snaplist()
{
    auto startTime = TTimeRef::get_nanoseconds_since_epoch();
    m_xposList.clear();
	m_xposLut.clear();
	m_xposBool.clear();
	
	// collects all clip boundaries and adds them to the snap list
        QList<AudioClip* > acList;
        Sheet* sheet = qobject_cast<Sheet*>(m_session);
        if (sheet) {
                acList.append(sheet->get_audioclip_manager()->get_clip_list());
        }
	
    SLPRINT("acList size is %d\n", acList.size());

	// Be able to snap to trackstart
        if (m_rangeStart == TTimeRef()) {
		m_xposList.append(TTimeRef());
	}

	for( int i = 0; i < acList.size(); i++ ) {

		AudioClip* clip = acList.at(i);
        if ( ! clip->get_location()->is_snappable()) {
			continue;
		}

		TTimeRef startlocation = clip->get_location()->get_start();
        TTimeRef endlocation = clip->get_location()->get_end();

		if (startlocation > endlocation) {
			PERROR("clip xstart > xend, this must be a programming error!");
			continue;  // something wrong, ignore this clip
		}
		if (startlocation >= m_rangeStart && startlocation <= m_rangeEnd) {
	 		m_xposList.append(startlocation);
		}
		if (endlocation >= m_rangeStart && endlocation <= m_rangeEnd) {
			m_xposList.append(endlocation);
		}
	}

	// add all on-screen markers
    QList<Marker*> markerList = m_session->get_timeline()->get_markers();
	for (int i = 0; i < markerList.size(); ++i) {
        if (markerList.at(i)->get_location()->is_snappable() && markerList.at(i)->get_location()->get_start() >= m_rangeStart && markerList.at(i)->get_location()->get_start() <= m_rangeEnd) {
            m_xposList.append(markerList.at(i)->get_location()->get_start());
		}
	}

	// add the working cursor's position
    TTimeRef worklocation = m_session->get_work_location();
	//printf("worklocation xpos is %d\n",  worklocation / m_scalefactor);
    if (m_session->get_work_snap()->is_snappable() && worklocation >= m_rangeStart && worklocation <= m_rangeEnd) {
        m_xposList.append(m_session->get_work_location());
	}
	

	// sort the list
    std::sort(m_xposList.begin(), m_xposList.end(), [](const TTimeRef& left, const TTimeRef& right){
        return left < right;
    });

    int range = int((m_rangeEnd - m_rangeStart) / m_scalefactor);

	// create a linear lookup table
	for (int i = 0; i <= range; ++i) {
		m_xposLut.push_back(TTimeRef());
		m_xposBool.push_back(false);
	}

	TTimeRef lastVal;
	long lastIndex = -1;
	// now modify the regions around snap points in the lookup table
	for (int i = 0; i < m_xposList.size(); i++) {
		if (lastIndex > -1 && m_xposList.at(i) == lastVal) {
			continue;  // check for duplicates and skip them
		}

		// check if neighbouring snap regions overlap.
		// if yes, reduce snap-range to keep the border in the middle
		int snaprange = config().get_property("Snap", "range", 10).toInt();
		int ls = - snaprange;

		if (lastIndex > -1) {
			if ( (m_xposList.at(i) - lastVal) < (2 * snaprange * m_scalefactor) ) {
                ls = - int((m_xposList.at(i) / m_scalefactor - lastVal / m_scalefactor) / 2);
			}
		}

		for (int j = ls; j <= snaprange; j++) {
            int pos = int((m_xposList.at(i) - m_rangeStart) / m_scalefactor + j); // index in the LUT

			if (pos < 0) {
				continue;
			}

			if (pos >= m_xposLut.size()) {
				break;
			}

			m_xposLut[pos] = m_xposList.at(i);
			m_xposBool[pos] = true;
		}

		lastVal = m_xposList.at(i);
		lastIndex = i;
	}
	
	m_isDirty = false;
    auto totaltime = TTimeRef::get_nanoseconds_since_epoch() - startTime;
    printf("SnapList::update_snaplist took: %ld\n", totaltime);
}


TTimeRef SnapList::get_snap_value(const TTimeRef& pos)
{
    bool didSnap;
    return get_snap_value(pos, didSnap);
}

// public function that checks if there is a snap position
// within +- snap-range of the supplied value i
TTimeRef SnapList::get_snap_value(const TTimeRef& pos, bool& didSnap)
{
    if (m_isDirty) {
        update_snaplist();
    }

    didSnap = false;

    int i = int((pos - m_rangeStart) / m_scalefactor);
    SLPRINT("get_snap_value:: i is %d\n", i);

    // catch dangerous values:
    if (i < 0) {
        SLPRINT("get_snap_value:: i < 0\n");
        return pos;
    }

    if (m_xposLut.isEmpty()) {
        SLPRINT("get_snap_value:: empty lut\n");
        return pos;
    }

    if (i >= m_xposLut.size()) {
        SLPRINT("get_snap_value:: i > m_xposLut.size()\n");
        return pos;
    }

    if (is_snap_value(pos)) {
        SLPRINT("get_snap_value returns: %s (was %s)\n", TTimeRef::timeref_to_ms_3(m_xposLut.at(i)).toLatin1().data(), TTimeRef::timeref_to_ms_3(pos).toLatin1().data());
        didSnap = true;
        return m_xposLut.at(i);
    }


    SLPRINT("get_snap_value returns: %s (was %s)\n", TTimeRef::timeref_to_ms_3(pos).toLatin1().data(), TTimeRef::timeref_to_ms_3(pos).toLatin1().data());
    return pos;
}

// returns true if i is inside a snap area, else returns false
bool SnapList::is_snap_value(const TTimeRef& pos)
{
	if (m_isDirty) {
		update_snaplist();
	}
	
    int i = int((pos - m_rangeStart) / m_scalefactor);
	SLPRINT("is_snap_value:: i is %d\n", i);
	
	// need to catch values outside the LUT. Return false in that case
	if (i < 0) {
		return false;
	}

	if (i >= m_xposBool.size()) {
		return false;
	}

    SLPRINT("is_snap_value returns, with contains: %d, %d\n", m_xposBool.at(i), m_xposList.contains(pos));
	return m_xposBool.at(i);
}

// returns the difference between the unsnapped and snapped location.
// The return value is negative if the supplied value is < snapped value
TTimeRef SnapList::get_snap_diff(const TTimeRef& pos)
{
	if (m_isDirty) {
		update_snaplist();
	}
	
    int i = int((pos - m_rangeStart) / m_scalefactor);
	SLPRINT("get_snap_diff:: i is %d\n", i);
	
	// need to catch values outside the LUT. Return 0 in that case
	if (i < 0) {
        return TTimeRef();
	}

	if (i >= m_xposLut.size()) {
        return TTimeRef();
	}

        SLPRINT("get_snap_diff returns: %s\n", TTimeRef::timeref_to_ms_3(m_xposLut.at(i)).toLatin1().data());
    return (pos - m_xposLut.at(i));
}

void SnapList::set_range(const TTimeRef& start, const TTimeRef& end, qint64 scalefactor)
{
    if (m_rangeStart == start && m_rangeEnd == end && m_scalefactor == scalefactor) {
        return;
    }

    SLPRINT("setting xstart %s, xend %s scalefactor %lld\n", TTimeRef::timeref_to_ms_3(start).toLatin1().data(), TTimeRef::timeref_to_ms_3(end).toLatin1().data(), scalefactor);

    m_rangeStart = start;
    m_rangeEnd = end;
	m_scalefactor = scalefactor;
	m_isDirty = true;
};

TTimeRef SnapList::next_snap_pos(const TTimeRef& pos)
{
	if (m_isDirty) {
		update_snaplist();
	}

    int index = int(pos / m_scalefactor);

    SLPRINT("next_snap_pos: index %d, m_xposLut size %d\n", index, m_xposLut.size());

	if (pos < TTimeRef()) {
		PERROR("pos < 0");
		return TTimeRef();
	}

    if (index >= m_xposLut.size()) {
        SLPRINT("index > m_xposLut.size() (index is %d)\n", index);
        index = m_xposLut.size() - 1;
    }

	TTimeRef newpos = pos;

    for (int i=index; i<m_xposLut.size(); ++i) {
        TTimeRef snap = m_xposLut.at(i);
        if (snap > pos) {
            newpos = snap;
            break;
        }
    }


    // maybe for later, not sure how to proceed with the SnapList logic
    // TTimeRef newWayLocation = m_xposList.at(closest_xposlist_index_for_location(pos));
    // printf("old way %s, new way %s\n", QS_C(TTimeRef::timeref_to_ms_3(newpos)), QS_C(TTimeRef::timeref_to_ms_3(newWayLocation)));

    return newpos;
}

TTimeRef SnapList::prev_snap_pos(const TTimeRef& pos)
{
	if (m_isDirty) {
		update_snaplist();
	}
	
	if (pos < TTimeRef()) {
		PERROR("pos < 0");
		return TTimeRef();
	}
	
	if (! m_xposLut.size()) {
		return pos;
	}
	
    int index = int(pos / m_scalefactor);

    SLPRINT("prev_snap_pos: index %d\n", index);

        if (index >= m_xposLut.size()) {
		index = m_xposLut.size() - 1;
	}
	
	TTimeRef newpos = pos;
	
	do {
		TTimeRef snap = m_xposLut.at(index);
		if (snap < pos && snap != TTimeRef()) {
			newpos = snap;
			break;
		}
		index--;
	} while (index >= 0);
	
	if (index == -1) {
		return TTimeRef();
	}

    // maybe for later, not sure how to proceed with the SnapList logic
    int closestIndex = closest_xposlist_index_for_location(pos) + 1;
    if (closestIndex >= m_xposList.size()) {
        closestIndex = m_xposList.size() - 1;
    }

    TTimeRef newWayLocation = pos;
    do {
        TTimeRef snap = m_xposList.at(closestIndex);
        if (snap < pos && snap != TTimeRef()) {
            newWayLocation = snap;
            break;
        }
        closestIndex--;
    } while (closestIndex >= 0);

    printf("old way %s, new way %s\n", QS_C(TTimeRef::timeref_to_ms_3(newpos)), QS_C(TTimeRef::timeref_to_ms_3(newWayLocation)));
	
	return newpos;
}


TTimeRef SnapList::calculate_snap_diff(const TTimeRef &leftlocation, const TTimeRef &rightlocation)
{
    TTimeRef snapStartDiff = TTimeRef();
    TTimeRef snapEndDiff = TTimeRef();
    TTimeRef snapDiff = TTimeRef();
	
	// check if there is anything to snap
	bool start_snapped = false;
	bool end_snapped = false;
	if (is_snap_value(leftlocation)) {
		start_snapped = true;
	}
	
	if (is_snap_value(rightlocation)) {
		end_snapped = true;
	}

	if (start_snapped) {
		snapStartDiff = get_snap_diff(leftlocation);
		snapDiff = snapStartDiff; // in case both ends snapped, change this value later, else leave it
	}

	if (end_snapped) {
		snapEndDiff = get_snap_diff(rightlocation); 
		snapDiff = snapEndDiff; // in case both ends snapped, change this value later, else leave it
	}

	// If both snapped, check which one is closer. Do not apply this check if one of the
	// ends hasn't snapped, because it's diff value will be 0 by default and will always
	// be smaller than the actually snapped value.
	if (start_snapped && end_snapped) {
        if (abs(snapEndDiff.universal_frame()) > abs(snapStartDiff.universal_frame()))
			snapDiff = snapStartDiff;
		else
			snapDiff = snapEndDiff;
	}
	
    return snapDiff;
}

bool SnapList::was_dirty()
{
	bool ret = m_wasDirty;
	m_wasDirty = false;
	return ret;
}

// TOOD: maybe usefull for later, it's at least fast
int SnapList::closest_xposlist_index_for_location(TTimeRef location)
{
    auto begin = m_xposList.begin();
    auto end = m_xposList.end();

    auto it = std::upper_bound(begin, end, location, [](const TTimeRef& left, const TTimeRef& right){
        return (left/* / m_scalefactor*/) < (right/* / m_scalefactor*/);
    });

    // first location is closest
    if (begin == it) {
        return 0;
    }

    // last location is closest. end is one past that.
    if (end == it) {
        return std::distance(begin, end) - 1;
    }

    return std::distance(begin, it);
}
