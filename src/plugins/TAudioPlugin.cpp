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

#include "TAudioPlugin.h"

#include "TAddRemoveCommand.h"
#include "TCurve.h"
#include "TSession.h"


TAudioPlugin::TAudioPlugin(TSession* session)
	: m_slave(nullptr)
        , m_session(session)
{
    m_bypass = false;
}

QDomNode TAudioPlugin::get_state(QDomDocument doc)
{
	QDomElement node = doc.createElement("Plugin");
	
	node.setAttribute("bypassed", is_bypassed());
	
	QDomNode controlPortsNode = doc.createElement("ControlPorts");
	foreach(TAudioPluginControlPort* port, m_controlPorts) {
		controlPortsNode.appendChild(port->get_state(doc));
	}
	
	if (!m_audioInputPorts.empty()) {
		QDomNode audioInputPortsNode = doc.createElement("AudioInputPorts");
		foreach(TAudioPluginInputPort* port, m_audioInputPorts) {
			audioInputPortsNode.appendChild(port->get_state(doc));
		}
		node.appendChild(audioInputPortsNode);
	}
	
	if (!m_audioOutputPorts.empty()) {
		QDomNode audioOutputPortsNode = doc.createElement("AudioOutputPorts");
		foreach(TAudioPluginOutputPort* port, m_audioOutputPorts) {
			audioOutputPortsNode.appendChild(port->get_state(doc));
		}
		node.appendChild(audioOutputPortsNode);
	}
	
	node.appendChild(controlPortsNode);
	
	return node;
}

int TAudioPlugin::set_state(const QDomNode & node)
{
	QDomElement e = node.toElement();
	
	m_bypass = e.attribute( "bypassed", "0").toInt();

	return 1;
}

TCommand* TAudioPlugin::toggle_bypass( )
{
	m_bypass = ! m_bypass;
	
	emit bypassChanged();
	
	return (TCommand*) nullptr;
}

TAudioPluginControlPort* TAudioPlugin::get_control_port_by_index(int index) const
{
	foreach(TAudioPluginControlPort* port, m_controlPorts) {
		if (port->get_index() == index) {
			return port;
		}
	}
	return nullptr;
}

void TAudioPlugin::automate_port(int index, bool automate)
{
	TAudioPluginControlPort* port = get_control_port_by_index(index);
	if (!port) {
//		PERROR("ControlPort with index %d does not exist", index);
		return;
	}
	port->set_use_automation(automate);
}


 

int TAudioPluginPort::set_state( const QDomNode & node )
{
	Q_UNUSED(node);
	return 1;
}































