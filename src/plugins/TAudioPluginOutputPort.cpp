
#include "TAudioPluginOutputPort.h"

TAudioPluginOutputPort::TAudioPluginOutputPort(QObject* parent, int index)
	: TAudioPluginPort(parent, index)
{
}

QDomNode TAudioPluginOutputPort::get_state( QDomDocument doc )
{
	QDomElement node = doc.createElement("AudioOutputPort");
	node.setAttribute("index", (int) m_index);
	
	return node;
}

int TAudioPluginOutputPort::set_state( const QDomNode & node )
{
	QDomElement e = node.toElement();
	
	m_index = e.attribute( "index", "-1").toInt();
	
	return 1;
}
