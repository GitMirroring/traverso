/*
Copyright (C) 2007 Remon Sijrier 

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

#include "TTimeLineRuler.h"

#include "TSession.h"
#include "TTimeLineMarker.h"
#include "TExportSpecification.h"
#include "Utils.h"
#include "TAddRemoveCommand.h"

#include <QRegularExpression>

#include "Debugger.h"

TTimeLineRuler::TTimeLineRuler(TSession * sheet)
	: TContextItem(sheet)
	, m_sheet(sheet)
{
    QObject::tr("TTimeLineRuler");
}

QDomNode TTimeLineRuler::get_state(QDomDocument doc)
{
	QDomElement domNode = doc.createElement("TimeLine");
	QDomNode markersNode = doc.createElement("Markers");
	domNode.appendChild(markersNode);

	foreach (TTimeLineMarker *marker, m_markers) {
		markersNode.appendChild(marker->get_state(doc));
	}

	return domNode;
}

int TTimeLineRuler::set_state(const QDomNode & node)
{
	m_markers.clear();

	QDomNode markersNode = node.firstChildElement("Markers");
	QDomNode markerNode = markersNode.firstChild();

	while (!markerNode.isNull()) {
		TTimeLineMarker* marker = new TTimeLineMarker(this, markerNode);
        connect(marker->get_location(), SIGNAL(locationChanged()), this, SLOT(marker_position_changed()));
		m_markers.append(marker);
		markerNode = markerNode.nextSibling();
	}

	index_markers();

	return 1;
}

TCommand * TTimeLineRuler::add_marker(TTimeLineMarker* marker, bool historable)
{
    connect(marker->get_location(), SIGNAL(locationChanged()), this, SLOT(marker_position_changed()));
	
	TAddRemoveCommand* cmd;
	cmd = new TAddRemoveCommand(this, marker, historable, m_sheet,
        "private_add_marker(TTimeLineMarker*)", "markerAdded(TTimeLineMarker*)",
        "private_remove_marker(TTimeLineMarker*)", "markerRemoved(TTimeLineMarker*)",
  		tr("Add Marker"));
	
	// Bypass the real time thread save logic in TSMP, since a Marker doesn't have YET
	// anything to do with audio processing routines.
	// WARNING: this should change as soon as Markers modify anything related to audio 
	// processing objects!!!!!!
	cmd->set_instantanious(true);

	return cmd;
}

TCommand* TTimeLineRuler::remove_marker(TTimeLineMarker* marker, bool historable)
{
    TAddRemoveCommand* cmd;
	cmd = new TAddRemoveCommand(this, marker, historable, m_sheet,
        "private_remove_marker(TTimeLineMarker*)", "markerRemoved(TTimeLineMarker*)",
        "private_add_marker(TTimeLineMarker*)", "markerAdded(TTimeLineMarker*)",
  		tr("Remove Marker"));
	
	// Bypass the real time thread save logic in TSMP, since a Marker doesn't have YET
	// anything to do with audio processing routines.
	// WARNING: this should change as soon as Markers modify anything related to audio 
	// processing objects!!!!!!
	cmd->set_instantanious(true);

	return cmd;
}

void TTimeLineRuler::private_add_marker(TTimeLineMarker * marker)
{
	m_markers.append(marker);
	index_markers();
}

void TTimeLineRuler::private_remove_marker(TTimeLineMarker * marker)
{
	m_markers.removeAll(marker);
	index_markers();
}

TTimeLineMarker * TTimeLineRuler::get_marker(qint64 id)
{
	foreach(TTimeLineMarker* marker, m_markers) {
		if (marker->get_id() == id) {
			return marker;
		}
	}
	
	return 0;
}

bool TTimeLineRuler::get_end_location(TTimeRef& location)
{
    for(TTimeLineMarker* marker : m_markers) {
		if (marker->get_type() == TTimeLineMarker::ENDMARKER) {
            location = marker->get_location()->get_start();
			return true;
		}
	}

	return false;
}

bool TTimeLineRuler::get_start_location(TTimeRef & location)
{
    if (m_markers.isEmpty()) {
        return false;
    }

    location = m_markers.first()->get_location()->get_start();
    return true;
}


bool TTimeLineRuler::has_end_marker()
{
	foreach(TTimeLineMarker* marker, m_markers) {
		if (marker->get_type() == TTimeLineMarker::ENDMARKER) {
			return true;
		}
	}

	return false;
}


TTimeLineMarker* TTimeLineRuler::get_end_marker()
{
	foreach(TTimeLineMarker* marker, m_markers) {
		if (marker->get_type() == TTimeLineMarker::ENDMARKER) {
			return marker;
		}
	}

    return nullptr;
}

void TTimeLineRuler::marker_position_changed()
{
	index_markers();

	emit markerPositionChanged();
	
	// FIXME This is not a fix to let the sheetview scrollbars 
	// know that it's range possably has to be recalculated!!!!!!!!!!!!!!
	emit m_sheet->lastFramePositionChanged();
}

void TTimeLineRuler::index_markers()
{
    std::sort(m_markers.begin(), m_markers.end(), [&](TTimeLineMarker* left, TTimeLineMarker* right) {
        return left->get_location()->get_start() < right->get_location()->get_start();
    });

	// let the markers know about their position (index)
	for (int i = 0; i < m_markers.size(); i++) {
		m_markers.at(i)->set_index(i+1);
	}	
}

// returns all markers of type CDTRACK
// sets 'endmarker' to true if an endmarker is present, else to false.
QList<TTimeLineMarker*> TTimeLineRuler::get_cd_layout(bool & endmarker)
{
        QList<TTimeLineMarker*> list;
        endmarker = false;

        foreach(TTimeLineMarker* marker, m_markers) {
                if (marker->get_type() == TTimeLineMarker::CDTRACK) {
                        list.append(marker);
                }

                if (marker->get_type() == TTimeLineMarker::ENDMARKER) {
                        list.append(marker);
                        endmarker = true;
                }
        }

        return list;
}


// formatting the track names in a separate function to guarantee that
// the file names of exported tracks and the entry in the TOC file always
// match
QString TTimeLineRuler::format_cdtrack_name(TTimeLineMarker *marker, int i)
{
	PENTER;
        QString name;
        QString song = marker->get_description();
        QString performer = marker->get_performer();

        name = QString("%1").arg(i, 2, 10, QChar('0'));

        if (!performer.isEmpty()) {
                name += "-" + performer;
        }

        if (!song.isEmpty()) {
                name += "-" + song;
        }

        name.replace(QRegularExpression("\\s"), "_");
        return name;
}

// creates a valid list of markers for CD export. Takes care of special cases
// such as if no markers are present, or if an end marker is missing.
QList<TTimeLineMarker*> TTimeLineRuler::get_cdtrack_list(TExportSpecification *spec)
{
	PENTER;
        bool endmarker;
        QList<TTimeLineMarker*> lst = get_cd_layout(endmarker);

	// FIXME: this function is called from the export thread, creating
	// new marker objects gives this warning:
	// QObject: Cannot create children for a parent that is in a different thread.

	// make sure there are at least a start- and end-marker in the list
        if (lst.size() == 0) {
                lst.push_back(new TTimeLineMarker(this, spec->get_export_start_location(), TTimeLineMarker::CDTRACK));
        }

        if (!endmarker) {
                TTimeRef endlocation = qMax(spec->get_export_end_location(), lst.last()->get_location()->get_start());
                lst.push_back(new TTimeLineMarker(this, endlocation, TTimeLineMarker::ENDMARKER));
        }

        return lst;
}


QString TTimeLineRuler::get_cdrdao_tracklist(TExportSpecification* spec, bool pregap)
{
        QString output;

        QList<TTimeLineMarker*> mlist = get_cdtrack_list(spec);

//	TTimeRef start;

        for(int i = 0; i < mlist.size()-1; ++i) {

                TTimeLineMarker* startmarker = mlist.at(i);
                TTimeLineMarker* endmarker = mlist.at(i+1);

                output += "TRACK AUDIO\n";

                if (startmarker->get_copyprotect()) {
                        output += "  NO COPY\n";
                } else {
                        output += "  COPY\n";
                }

                if (startmarker->get_preemphasis()) {
                        output += "  PRE_EMPHASIS\n";
                }

                output += "  CD_TEXT {\n    LANGUAGE 0 {\n";
                output += "      TITLE \"" + startmarker->get_description() + "\"\n";
                output += "      PERFORMER \"" + startmarker->get_performer() + "\"\n";
                output += "      ISRC \"" + startmarker->get_isrc() + "\"\n";
                output += "      ARRANGER \"" + startmarker->get_arranger() + "\"\n";
                output += "      SONGWRITER \"" + startmarker->get_songwriter() + "\"\n";
                output += "      MESSAGE \"" + startmarker->get_message() + "\"\n    }\n  }\n";

                // add some stuff only required for the first track (e.g. pre-gap)
                if ((i == 0) && pregap) {
                        //if (start == 0) {
                                // standard pregap, because we have a track marker at the beginning
                                output += "  PREGAP 00:02:00\n";
                        //} else {
                        //	// no track marker at the beginning, thus use the part from 0 to the first
                        //	// track marker for the pregap
                        //	output += "  START " + frame_to_cd(start, m_project->get_rate()) + "\n";
                        //	start = 0;
                        //}
                }

                TTimeRef length = TTimeRef::cd_to_timeref(TTimeRef::timeref_to_cd(endmarker->get_location()->get_start())) - TTimeRef::cd_to_timeref(TTimeRef::timeref_to_cd(startmarker->get_location()->get_start()));

//		QString s_start = TTimeRef::timeref_to_cd(start);
                QString s_length = TTimeRef::timeref_to_cd(length);

//		output += "  FILE \"" + spec->name + "." + spec->extraFormat["filetype"] + "\" " + s_start + " " + s_length + "\n\n";
                output += "  FILE \"" + format_cdtrack_name(startmarker, i+1) + "." + spec->extraFormat["filetype"] + "\" 0 " + s_length + "\n\n";
//		start += length;

                // check if the second marker is of type "Endmarker"
                if (endmarker->get_type() == TTimeLineMarker::ENDMARKER) {
                        break;
                }
        }

        return output;
}
