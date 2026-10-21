#pragma once

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

