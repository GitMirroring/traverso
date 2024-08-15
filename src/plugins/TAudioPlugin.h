/*
Copyright (C) 2006-2024 Remon Sijrier

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


#ifndef T_AUDIO_PLUGIN_H
#define T_AUDIO_PLUGIN_H

#include "ContextItem.h"
#include <QString>
#include <QDomNode>

#include "TAudioPluginControlPort.h"
#include "TAudioPluginInputPort.h"
#include "TAudioPluginOutputPort.h"
#include "defines.h"

class TSession;
class AudioBus;

struct TAudioPluginInfo {
    TAudioPluginInfo() {
        audioPortInCount = 0;
        audioPortOutCount = 0;
    }
    int audioPortInCount;
    int audioPortOutCount;
    QString type;
    QString name;
    QString uri;
};

class TAudioPlugin : public ContextItem
{
    Q_OBJECT

public:
    TAudioPlugin(TSession* session = nullptr);
    virtual ~TAudioPlugin(){}

    virtual int init() {return 1;}
    virtual	QDomNode get_state(QDomDocument doc);
    virtual int set_state(const QDomNode & node );
    virtual void process(AudioBus* bus, nframes_t nframes) = 0;
    virtual QString get_name() = 0;

    TAudioPluginControlPort* get_control_port_by_index(int index) const;
    QList<TAudioPluginControlPort* > get_control_ports() const { return m_controlPorts; }

    TAudioPlugin* get_slave() const {return m_slave;}
    TSession* get_session() const {return m_session;}
    bool is_bypassed() const {return m_bypass;}

    void automate_port(int index, bool automate);

    bool operator<(const TAudioPlugin& /*other*/) {
        return true;
    }

    TAudioPlugin* next = nullptr;

protected:
    TAudioPlugin*                         m_slave;
    TSession*                       m_session;
    QList<TAudioPluginControlPort* > 	m_controlPorts;
    QList<TAudioPluginInputPort* >		m_audioInputPorts;
    QList<TAudioPluginOutputPort* >	m_audioOutputPorts;

    bool	m_bypass;


signals:
    void bypassChanged();

public slots:
    TCommand* toggle_bypass();
};














#endif

//eof
