/*Copyright (C) 2007 Remon Sijrier

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

#include "GainEnvelope.h"

#include "TSheet.h"
#include "TCurve.h"
#include "AudioBus.h"

GainEnvelope::GainEnvelope(TSession* session)
        : TAudioPlugin(session)
{
    m_gain = 1.0f;
	TAudioPluginControlPort* port = new TAudioPluginControlPort(this, 0, 1.0);
	port->set_index(0);
	m_controlPorts.append(port);
        if (session) {
                set_session(session);
	}
}

QDomNode GainEnvelope::get_state(QDomDocument doc)
{
	QDomElement node = TAudioPlugin::get_state(doc).toElement();
	node.setAttribute("type", "GainEnvelope");
	node.setAttribute("gain", m_gain);
	
	return node;
}

int GainEnvelope::set_state(const QDomNode & node)
{
	foreach(TAudioPluginControlPort* port, m_controlPorts) {
		delete port;
	}
	m_controlPorts.clear();
	
	TAudioPlugin::set_state(node);
	
	QDomElement controlPortsNode = node.firstChildElement("ControlPorts");
	if (!controlPortsNode.isNull()) {
		QDomNode portNode = controlPortsNode.firstChild();
		
		while (!portNode.isNull()) {
			
			TAudioPluginControlPort* port = new TAudioPluginControlPort(this, portNode);
			m_controlPorts.append(port);
			
			portNode = portNode.nextSibling();
		}
	}
	
	QDomElement e = node.toElement();
	m_gain = e.attribute("gain", "1.0").toFloat();
	
	return 1;
}

QString GainEnvelope::get_name()
{
	return "Gain Envelope";
}

void GainEnvelope::set_session(TSession * session)
{
    m_session = session;
    set_history_stack(m_session->get_history_stack());

	if (get_curve()) {
                get_curve()->set_sheet(session);
	}
}

void GainEnvelope::process(AudioBus * bus, nframes_t nframes)
{
    for (uint chan=0; chan<bus->get_channel_count(); ++chan) {
        bus->get_buffer(chan).apply_gain_to_buffer(nframes, m_gain);
    }
}

TCurve * GainEnvelope::get_curve()
{
    if (m_controlPorts.size() && !m_controlPorts.at(0)->get_curve()) {
        // no automation was setup it seems, do it now!
        automate_port(0, true);
    }
    return m_controlPorts.at(0)->get_curve();
}


void GainEnvelope::process_gain(AudioBus* audioBus, const TTimeRef& startlocation, const TTimeRef& endlocation, nframes_t nframes, uint channels)
{
    TAudioPluginControlPort* port = m_controlPorts.at(0);

    if (port->use_automation()) {
        port->get_curve()->process(audioBus, startlocation, endlocation, nframes, channels, m_gain);
    } else {
        audioBus->apply_gain_to_buffers(nframes, get_gain());
    }
}

QString GainEnvelope::get_gain_db_string(int decimals)
{
    float db = Mixer::coefficient_to_dB (get_gain());

    QString gainIndB;

    if (std::fabs(db) < (1/::pow(10, decimals))) {
        db = 0.0f;
    }

    if ( db < -99 )
        gainIndB = "- INF";
    else if ( db < 0 )
        gainIndB = "- " + QByteArray::number ( ( -1 * db ), 'f', decimals ) + " dB";
    else if ( db > 0 )
        gainIndB = "+ " + QByteArray::number ( db, 'f', decimals ) + " dB";
    else {
        gainIndB = "  " + QByteArray::number ( db, 'f', decimals ) + " dB";
    }

    return gainIndB;
}
