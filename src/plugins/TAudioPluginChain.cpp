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
 
#include "TAudioPluginChain.h"

#include "TAudioPlugin.h"
#include "PluginManager.h"
#include "TInputEventDispatcher.h"
#include "TSession.h"
#include "TAddRemoveCommand.h"
#include "GainEnvelope.h"
#include "TInformUser.h"



#include "Debugger.h"

TAudioPluginChain::TAudioPluginChain(ContextItem* parent, TSession* session)
	: ContextItem(parent)
{
    m_fader = new GainEnvelope(session);
    private_add_plugin(m_fader);
    private_plugin_added(m_fader);

    set_session(session);

    connect(this, SIGNAL(privatePluginAdded(TAudioPlugin*)), this, SLOT(private_plugin_added(TAudioPlugin*)));
    connect(this, SIGNAL(privatePluginRemoved(TAudioPlugin*)), this, SLOT(private_plugin_removed(TAudioPlugin*)));
}

TAudioPluginChain::~ TAudioPluginChain()
{
	PENTERDES;
    for(auto plugin : m_plugins) {
        delete plugin;
	}
}


QDomNode TAudioPluginChain::get_state(QDomDocument doc)
{
	QDomNode pluginsNode = doc.createElement("Plugins");
	
    for(TAudioPlugin* plugin : m_plugins) {
        if (plugin == m_fader) {
            continue;
        }
		pluginsNode.appendChild(plugin->get_state(doc));
	}
	
    pluginsNode.appendChild(m_fader->get_state(doc));
	
	return pluginsNode;
}

int TAudioPluginChain::set_state( const QDomNode & node )
{
	QDomNode pluginsNode = node.firstChildElement("Plugins");
	QDomNode pluginNode = pluginsNode.firstChild();
	
	while(!pluginNode.isNull()) {
		if (pluginNode.toElement().attribute( "type", "") == "GainEnvelope") {
			m_fader->set_state(pluginNode);
		} else {
			TAudioPlugin* plugin = PluginManager::instance()->get_plugin(pluginNode);
			if (!plugin) {
				pluginNode = pluginNode.nextSibling();
				continue;
			}
            plugin->set_history_stack(get_history_stack());
			private_add_plugin(plugin);
            private_plugin_added(plugin);
		}
		
		pluginNode = pluginNode.nextSibling();
	}
	
	return 1;
}


TCommand* TAudioPluginChain::add_plugin(TAudioPlugin * plugin, bool historable)
{
    plugin->set_history_stack(get_history_stack());

    return new TAddRemoveCommand( this, plugin, historable, m_session,
                          "private_add_plugin(TAudioPlugin*)", "privatePluginAdded(TAudioPlugin*)",
                          "private_remove_plugin(TAudioPlugin*)", "privatePluginRemoved(TAudioPlugin*)",
                          tr("Add Plugin (%1)").arg(plugin->get_name()));
}


TCommand* TAudioPluginChain::remove_plugin(TAudioPlugin* plugin, bool historable)
{
    if (plugin == m_fader) {
        // do not remove fader we always have one
        tInformUser().information(tr("Gain Envelope (Fader) is not removable"));
        return ied().failure();
    }

    return new TAddRemoveCommand( this, plugin, historable, m_session,
                          "private_remove_plugin(TAudioPlugin*)", "privatePluginRemoved(TAudioPlugin*)",
                          "private_add_plugin(TAudioPlugin*)", "privatePluginAdded(TAudioPlugin*)",
                          tr("Remove Plugin (%1)").arg(plugin->get_name()));
}


void TAudioPluginChain::private_add_plugin( TAudioPlugin * plugin )
{
    m_rtPlugins.append(plugin);
}


void TAudioPluginChain::private_remove_plugin( TAudioPlugin * plugin )
{
    if (!m_rtPlugins.remove(plugin)) {
		PERROR("Plugin not found in list, this is invalid plugin remove!!!!!");
    }
}

void TAudioPluginChain::private_plugin_added(TAudioPlugin *plugin)
{
    m_plugins.append(plugin);
    emit pluginAdded(plugin);
}

void TAudioPluginChain::private_plugin_removed(TAudioPlugin *plugin)
{
    m_plugins.removeAll(plugin);
    emit pluginRemoved(plugin);
}

void TAudioPluginChain::set_session(TSession * session)
{
    if (!session) {
        return;
    }

    m_session = session;
    set_history_stack(m_session->get_history_stack());
    m_fader->set_session(session);
}

QList<TAudioPlugin *> TAudioPluginChain::get_pre_fader_plugins()
{
    QList<TAudioPlugin*> preFaderPlugins;
    for(TAudioPlugin* plugin : m_plugins) {
        if (plugin == m_fader) {
            return preFaderPlugins;
        } else {
            preFaderPlugins.append(plugin);
        }
    }

    return preFaderPlugins;
}

QList<TAudioPlugin *> TAudioPluginChain::get_post_fader_plugins()
{
    QList<TAudioPlugin*> postFaderPlugins;
    bool faderWasReached = false;

    for(TAudioPlugin* plugin : m_plugins) {
        if (faderWasReached) {
            postFaderPlugins.append(plugin);
        } else if (plugin == m_fader) {
            faderWasReached = true;
        }
    }

    return postFaderPlugins;
}


void TAudioPluginChain::process_pre_fader(AudioBus *bus, nframes_t nframes)
{
    for(TAudioPlugin* plugin = m_rtPlugins.first(); plugin != nullptr; plugin = plugin->next) {
        if (plugin == m_fader) {
            return;
        }
        plugin->process(bus, nframes);
    }
}

int TAudioPluginChain::process_post_fader(AudioBus *bus, nframes_t nframes)
{
    if (!m_rtPlugins.size()) {
        return 0;
    }

    bool faderWasReached = false;

    for(TAudioPlugin* plugin = m_rtPlugins.first(); plugin != nullptr; plugin = plugin->next) {
        if (faderWasReached) {
            plugin->process(bus, nframes);
        } else if (plugin == m_fader) {
            faderWasReached = true;
        }
    }

    return 1;
}
