
#include "TAudioPluginPort.h"

QDomNode TAudioPluginPort::get_state( QDomDocument doc )
{
	QDomElement node = doc.createElement("ControlPort");
	node.setAttribute("index", (int) m_index);
	
	return node;
}
