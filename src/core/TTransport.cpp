/*
Copyright (C) 2011 Remon Sijrier

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

#include "TTransport.h"

#include "TSnapList.h"
#include "TCommand.h"
#include "TSession.h"
#include "TSheet.h"
#include "TProject.h"
#include "TInputEventDispatcher.h"

#include "TTimeLineRuler.h"

TTransport::TTransport()
{
    m_skipTimer.setSingleShot(true);

}

TTransport& transport()
{
    static TTransport transport;
    return transport;
}

TCommand* TTransport::start_transport()
{
    if (!m_session)	{
        return nullptr;
    }

    return m_session->start_transport();
}

TCommand * TTransport::set_recordable_and_start_transport()
{
	if (m_project) {
		TSheet* sheet = m_project->get_active_sheet();
		if (sheet) {
			return sheet->set_recordable_and_start_transport();
		}
	}

    return nullptr;
}

TCommand * TTransport::to_start()
{
	if (m_session)
	{
		m_session->set_transport_location(TTimeRef());
	}

    return nullptr;
}

TCommand* TTransport::to_end()
{
	if (m_session)
	{
		m_session->set_transport_location(m_session->get_last_location());
	}

    return nullptr;
}

// the timer is used to allow 'hopping' to the left from snap position to snap position
// even during playback.
TCommand* TTransport::prev_skip_pos()
{
    TTimeRef location = m_session->get_snap_list()->prev_snap_pos(m_session->get_transport_location());

    if (m_skipTimer.isActive()) {
        location = m_session->get_snap_list()->prev_snap_pos(location);
    }

    m_skipTimer.start(500);

    m_session->set_transport_location(location);

    return ied().succes();
}

TCommand* TTransport::next_skip_pos()
{
    TTimeRef location = m_session->get_snap_list()->next_snap_pos(m_session->get_transport_location());
    m_session->set_transport_location(location);

    return ied().succes();
}
