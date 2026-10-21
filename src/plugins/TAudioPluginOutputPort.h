#pragma once

#include "TAudioPluginPort.h"

class TAudioPluginOutputPort : public TAudioPluginPort
{

public:
    TAudioPluginOutputPort(QObject* parent, int index);
    TAudioPluginOutputPort(QObject* parent) : TAudioPluginPort(parent) {}
    virtual ~TAudioPluginOutputPort(){}

    QDomNode get_state(QDomDocument doc);
    int set_state( const QDomNode & node );

};

