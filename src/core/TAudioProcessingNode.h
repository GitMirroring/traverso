/*
    Copyright (C) 2010 - 2026 Remon Sijrier
    This file is part of Traverso
*/

#pragma once

#include "TCommand.h"
#include "TContextItem.h"

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
    bool            m_isMuted;
    float           m_pan;

private:
    QPointer<QPropertyAnimation>  m_gainAnimation;

public slots:
    float get_gain();
    QString get_gain_db_string(int decimals=1);

    void set_gain(float gain);
    void set_gain_animated(float gain);
    TCommand* mute();

signals:
    void audibleStateChanged();
    void stateChanged();
    void muteChanged(bool isMuted);
    void panChanged();
    void gainChanged();
};

