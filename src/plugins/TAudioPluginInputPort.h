#ifndef TAUDIOPLUGININPUTPORT_H
#define TAUDIOPLUGININPUTPORT_H

#include "TAudioPluginPort.h"


class TAudioPluginInputPort : public TAudioPluginPort
{

public:
    TAudioPluginInputPort(QObject* parent, int index);
    TAudioPluginInputPort(QObject* parent) : TAudioPluginPort(parent) {};
    virtual ~TAudioPluginInputPort(){};

    QDomNode get_state(QDomDocument doc);
    int set_state( const QDomNode & node );

};

#endif // TAUDIOPLUGININPUTPORT_H
