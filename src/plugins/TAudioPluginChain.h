/*
Copyright (C) 2006-2019 Remon Sijrier

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


#ifndef T_AUDIO_PLUGIN_CHAIN_H
#define T_AUDIO_PLUGIN_CHAIN_H

#include <ContextItem.h>
#include <QList>
#include <QDomNode>
#include "TAudioPlugin.h"
#include "GainEnvelope.h"
#include "TRealTimeLinkedList.h"

class TSession;
class AudioBus;

class TAudioPluginChain : public ContextItem
{
    Q_OBJECT

public:
    TAudioPluginChain(ContextItem* parent, TSession* session=nullptr);
    ~TAudioPluginChain();

    QDomNode get_state(QDomDocument doc);
    int set_state(const QDomNode & node );

    TCommand* add_plugin(TAudioPlugin* plugin, bool historable=true);
    TCommand* remove_plugin(TAudioPlugin* plugin, bool historable=true);
    void process_pre_fader(AudioBus* bus, nframes_t nframes);
    int process_post_fader(AudioBus* bus, nframes_t nframes);

    void set_session(TSession* session);

    QList<TAudioPlugin*>  get_plugins() const {return m_plugins;}
    QList<TAudioPlugin*>  get_pre_fader_plugins();
    QList<TAudioPlugin*>  get_post_fader_plugins();
    GainEnvelope*   get_fader() const {return m_fader;}

private:
    TRealTimeLinkedList<TAudioPlugin*>	m_rtPlugins;
    QList<TAudioPlugin*>  m_plugins;
    GainEnvelope*	m_fader;
    TSession*	m_session{};

private slots:
    void private_add_plugin(TAudioPlugin* plugin);
    void private_remove_plugin(TAudioPlugin* plugin);
    void private_plugin_added(TAudioPlugin* plugin);
    void private_plugin_removed(TAudioPlugin* plugin);

signals:
    void pluginAdded(TAudioPlugin* plugin);
    void pluginRemoved(TAudioPlugin* plugin);
    void privatePluginRemoved(TAudioPlugin*);
    void privatePluginAdded(TAudioPlugin*);
};

#endif

//eof
