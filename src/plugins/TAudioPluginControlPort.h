#ifndef TAUDIOPLUGINCONTROLPORT_H
#define TAUDIOPLUGINCONTROLPORT_H

#include "TAudioPluginPort.h"

class TAudioPlugin;
class Curve;

class TAudioPluginControlPort : public TAudioPluginPort
{
    Q_OBJECT

public:
    TAudioPluginControlPort(TAudioPlugin* parent, int index, float value);
    TAudioPluginControlPort(TAudioPlugin* parent, const QDomNode node);
    virtual ~TAudioPluginControlPort(){}

    virtual float get_control_value() {return m_value; }
    virtual float get_min_control_value() {return m_min;}
    virtual float get_max_control_value() {return m_max;}
    virtual float get_default_value() {return m_default;}

    void set_min(float min) {m_min = min;}
    void set_max(float max) {m_max = max;}
    void set_default(float def) {m_default = def;}
    void set_use_automation(bool automation);

    bool use_automation();
    Curve* get_curve() const {return m_curve;}

    virtual QDomNode get_state(QDomDocument doc);

    virtual QString get_description();
    virtual QString get_symbol();

protected:
    Curve*	m_curve;
    TAudioPlugin*	m_plugin;
    float	m_value{};
    float 	m_default{};
    float	m_min{};
    float	m_max{};
    bool	m_automation;
    QString m_description;

    virtual int set_state( const QDomNode & node );

public slots:
    void set_control_value(float value);
};

#endif // TAUDIOPLUGINCONTROLPORT_H
