
#include "TAudioPluginControlPort.h"
#include "TAddRemoveCommand.h"
#include "TCurveNode.h"
#include "TAudioPlugin.h"
#include "TAudioPluginPort.h"
#include "TCurve.h"

TAudioPluginControlPort::TAudioPluginControlPort(TAudioPlugin* parent, int index, float value)
	: TAudioPluginPort(parent, index)
	, m_curve(nullptr)
	, m_plugin(parent)
	, m_value(value)
	, m_automation(false)
{
}

TAudioPluginControlPort::TAudioPluginControlPort(TAudioPlugin* parent, const QDomNode node)
	: TAudioPluginPort(parent)
	, m_curve(nullptr)
	, m_plugin(parent)
	, m_automation(false)
{
    TAudioPluginControlPort::set_state(node);
}

QDomNode TAudioPluginControlPort::get_state(QDomDocument doc)
{
	QDomElement node = TAudioPluginPort::get_state(doc).toElement();
	node.setAttribute("value", m_value);
	node.setAttribute("automation", m_automation);
	if (m_curve) {
		node.appendChild(m_curve->get_state(doc, "PortAutomation"));
	}
	return node;
}

int TAudioPluginControlPort::set_state(const QDomNode & node)
{
	QDomElement e = node.toElement();
	m_index = e.attribute("index", "-1").toInt();
	m_value = e.attribute("value", "nan").toFloat();
	m_automation = e.attribute("automation", "0").toInt();
	
	QDomElement curveNode = node.firstChildElement("PortAutomation");
	if (!curveNode.isNull()) {
		m_curve = new TCurve(m_plugin, curveNode);
                m_curve->set_sheet(m_plugin->get_session());
	}
		
	return 1;
}

QString TAudioPluginControlPort::get_description()
{
	return m_description;
}

QString TAudioPluginControlPort::get_symbol()
{
	return "";
}

void TAudioPluginControlPort::set_control_value(float value)
{
	m_value = value;
}

void TAudioPluginControlPort::set_use_automation(bool automation)
{
	m_automation = automation;
	if (!m_curve) {
		m_curve = new TCurve(m_plugin);
		// Add the first default node:
        TCurveNode* node = new TCurveNode(m_curve, 0.0, 1.0);
		TAddRemoveCommand* cmd = (TAddRemoveCommand*)m_curve->add_node(node, false);
		cmd->set_instantanious(true);
		TCommand::process_command(cmd);
                if (m_plugin->get_session()) {
                        m_curve->set_sheet(m_plugin->get_session());
		}
	}
}

bool TAudioPluginControlPort::use_automation()
{
	return m_automation;
}
