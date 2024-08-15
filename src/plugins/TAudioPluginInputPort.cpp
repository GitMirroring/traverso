
#include "TAudioPluginInputPort.h"

TAudioPluginInputPort::TAudioPluginInputPort(QObject* parent, int index)
	: TAudioPluginPort(parent, index)
{
}

QDomNode TAudioPluginInputPort::get_state( QDomDocument doc )
{
	QDomElement node = doc.createElement("AudioInputPort");
	node.setAttribute("index", m_index);
	
	return node;
}

int TAudioPluginInputPort::set_state( const QDomNode & node )
{
	QDomElement e = node.toElement();
	
	m_index = e.attribute( "index", "-1").toInt();
	
	return 1;
}
