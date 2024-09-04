/*
Copyright (C) 2010 Remon Sijrier

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

#ifndef T_AUDIO_PROCESSING_NODE_H
#define T_AUDIO_PROCESSING_NODE_H

#include "TCommand.h"
#include "TContextItem.h"
#include "defines.h"

#include <QPointer>
#include <QPropertyAnimation>

class AudioBus;
class TAudioClip;
class TAudioPlugin;
class TAudioPluginChain;
class TSession;
class GainEnvelope;


class TAudioProcessingNode : public TContextItem
{
    Q_OBJECT
    Q_PROPERTY(float gain READ get_gain WRITE set_gain NOTIFY gainChanged)


public:
    TAudioProcessingNode (TSession* session=0);
    virtual ~TAudioProcessingNode () {}

    TCommand* add_plugin(TAudioPlugin* plugin);
    TCommand* remove_plugin(TAudioPlugin* plugin);

    TAudioPluginChain* get_plugin_chain() const {return m_pluginChain;}
    TSession* get_session() const {return m_session;}
    QString get_name() const {return m_name;}
    float get_pan() const {return m_pan;}

    void set_muted(bool muted);
    virtual void set_name(const QString& name);
    void set_pan(float pan);

    bool is_muted() const {return m_isMuted;}


protected:
    TSession*       m_session;
    AudioBus*       m_processBus;
    GainEnvelope*   m_fader;
    TAudioPluginChain*    m_pluginChain;
    QString         m_name;
    audio_sample_t  m_maxGainAmplification;
    bool            m_isMuted;
    float           m_pan;

private:
    QPointer<QPropertyAnimation>  m_gainAnimation;


    float m_gain;

public slots:
    float get_gain();
    QString get_gain_db_string(int decimals=1);

    void set_gain(float gain);
    void set_gain_animated(float gain);
    DISPATCH_RULE_IS_ALWAYS TCommand* mute();

signals:
    void audibleStateChanged();
    void stateChanged();
    void muteChanged(bool isMuted);
    void panChanged();
    void gainChanged();
};


#endif
