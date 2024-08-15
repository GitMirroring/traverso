#ifndef TAUDIOPLUGINPORT_H
#define TAUDIOPLUGINPORT_H

#include "qdom.h"
#include "qobject.h"

class TAudioPluginPort : public QObject
{

public:
    TAudioPluginPort(QObject* parent, int index) : QObject(parent), m_index(index), m_hint(FLOAT_CONTROL) {}
    TAudioPluginPort(QObject* parent) : QObject(parent), m_hint(FLOAT_CONTROL) {}
    virtual ~TAudioPluginPort(){}

    virtual QDomNode get_state(QDomDocument doc);
    virtual int set_state( const QDomNode & node ) = 0;

    enum PortHint {
        FLOAT_CONTROL,
        INT_CONTROL,
        LOG_CONTROL
    };

    int get_index() const {return m_index;}
    int get_hint() const {return m_hint;}

    void set_index(int index) {m_index = index;}

protected:
    int	m_index;
    int	m_hint;
};

#endif // TAUDIOPLUGINPORT_H
