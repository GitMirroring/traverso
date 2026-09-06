/*
    Copyright (C) 2005-224 Remon Sijrier
 
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

#include "TJackDriver.h"
#include "Utils.h"

#include <jack/jack.h>

#if defined (ALSA_SUPPORT)
#include "TAlsaDriver.h"
#endif

#include "TAudioDevice.h"
#include "AudioChannel.h"
#include "TAudioThreadMessageQueue.h"
#include "TTimeRef.h"



#include "Debugger.h"

/**
 * Used for the type argument of jack_port_register() for default
 * audio ports.
 */
#define JACK_DEFAULT_AUDIO_TYPE "32 bit float mono audio"


TJackDriver::TJackDriver(TAudioDevice* device)
    : TAudioDriver(device)
{
    read = TAudioDriverReadWriteCallBack(this, &TJackDriver::_read);
    write = TAudioDriverReadWriteCallBack(this, &TJackDriver::_write);
    run_cycle = RunCycleCallback(this, &TJackDriver::_run_cycle);

    m_jackClient = nullptr;
    m_isSlave = false;
    m_running = 0;

    connect(this, &TJackDriver::pcpairRemoved, this, &TJackDriver::cleanup_removed_port_channel_pair);
}

TJackDriver::~TJackDriver( )
{
    PENTER;
    Q_ASSERT(!is_running());

    if (m_running == 2) {
        // jack server shut us down so do not call jack_client_close
        return;
    }

    jack_client_close (m_jackClient);
}

int TJackDriver::_read( nframes_t nframes )
{
    for (int i=0; i<m_inputs.size(); i++) {
        PortChannelPair* pcpair = m_inputs.at(i);

        pcpair->channel->read_from_hardware_port((audio_sample_t*)jack_port_get_buffer (pcpair->jackport, nframes), nframes);
    }
    return 1;
}

int TJackDriver::_write( nframes_t nframes )
{
    for (int i=0; i<m_outputs.size(); i++) {
        PortChannelPair* pcpair = m_outputs.at(i);

        memcpy (jack_port_get_buffer (pcpair->jackport, nframes), pcpair->channel->get_buffer().get_data(nframes), sizeof (jack_default_audio_sample_t) * nframes);
        pcpair->channel->silence_buffer();
    }
    return 1;
}

int TJackDriver::setup(QList<AudioChannel* > channels, const QString &projectName)
{
    PENTER;

    QString clientName = QString("Traverso_" + projectName);
    clientName = clientName.replace(" ", "_");
    m_jackClient = nullptr;
    m_captureFrameLatency = m_playbackFrameLatency =0 ;


    printf("Connecting to the Jack server...\n");

    if ( (m_jackClient = jack_client_open(QS_C(clientName), JackNoStartServer, nullptr)) == nullptr) {
        emit driverSetupMessage("Jack", tr("Couldn't connect to the jack server, is jack running?"), TAudioDevice::DRIVER_SETUP_FAILURE);
        return -1;
    }

    for(AudioChannel* channel : channels) {
        add_channel(channel);
    }

    return 1;
}


void TJackDriver::add_channel(AudioChannel* channel)
{
    PENTER;
    PortChannelPair* pcpair = new PortChannelPair();

    if (channel->get_type() == AudioChannel::ChannelIsInput) {
        pcpair->jackport = jack_port_register (m_jackClient, channel->get_name().toUtf8().data(), JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
    }

    if (channel->get_type() == AudioChannel::ChannelIsOutput) {
        pcpair->jackport = jack_port_register (m_jackClient, channel->get_name().toUtf8().data(), JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    }

    if (pcpair->jackport == nullptr) {
        printf("JackDriver: cannot register port \"%s\"!\n", channel->get_name().toUtf8().data());
        delete pcpair;
        return;
    }

    pcpair->channel = channel;
    pcpair->channel->set_latency( m_framesPerCycle + m_captureFrameLatency );
    pcpair->name = channel->get_name();


    if (is_running()) {
        tsmp().add_gui_event(this, pcpair, "private_add_port_channel_pair(PortChannelPair*)", "");
    } else {
        private_add_port_channel_pair(pcpair);
    }
}

void TJackDriver::private_add_port_channel_pair(PortChannelPair *pair)
{
    if (pair->channel->get_type() == AudioChannel::ChannelIsInput) {
        m_inputs.append(pair);
    }
    if (pair->channel->get_type() == AudioChannel::ChannelIsOutput) {
        m_outputs.append(pair);
    }
}

void TJackDriver::private_remove_port_channel_pair(PortChannelPair *pair)
{
    if (pair->channel->get_type() == AudioChannel::ChannelIsInput) {
        m_inputs.removeAll(pair);
    }
    if (pair->channel->get_type() == AudioChannel::ChannelIsOutput) {
        m_outputs.removeAll(pair);
    }
}

void TJackDriver::remove_channel(AudioChannel* channel)
{
    PENTER;
    foreach(PortChannelPair* pcpair, m_outputs) {

        if (pcpair->channel == channel) {
            tsmp().add_gui_event(this, pcpair, "private_remove_port_channel_pair(PortChannelPair*)", "pcpairRemoved(PortChannelPair*)");
            return;
        }
    }

    foreach(PortChannelPair* pcpair, m_inputs) {

        if (pcpair->channel == channel) {
            tsmp().add_gui_event(this, pcpair, "private_remove_port_channel_pair(PortChannelPair*)", "pcpairRemoved(PortChannelPair*)");
            return;
        }
    }
}

int TJackDriver::attach( )
{
    PENTER;

    m_device->set_buffer_size( jack_get_buffer_size(m_jackClient) );
    m_device->set_sample_rate (jack_get_sample_rate(m_jackClient));

    jack_set_process_callback (m_jackClient, _process_callback, this);
    jack_set_xrun_callback (m_jackClient, _xrun_callback, this);
    jack_set_buffer_size_callback (m_jackClient, _bufsize_callback, this);
    jack_set_freewheel_callback(m_jackClient, _freewheel_callback, this);
    jack_on_shutdown(m_jackClient, _on_jack_shutdown_callback, this);

    update_config();

    return 1;
}

int TJackDriver::start( )
{
    PENTER;
    if (jack_activate (m_jackClient)) {
        //if jack_activate() != 0, something went wrong!
        return -1;
    }

    emit driverSetupMessage("Jack", tr("Succesfully connected to jack server!"), TAudioDevice::DRIVER_SETUP_SUCCESS);

    m_running = 1;
    return 1;
}

int TJackDriver::stop( )
{
    PENTER;
    jack_deactivate(m_jackClient);

    m_running = 0;

    return 1;
}

int TJackDriver::process_callback (nframes_t nframes)
{
    jack_position_t pos;
    jack_transport_state_t state = jack_transport_query (m_jackClient, &pos);

    m_transportControl.set_state(state);
    m_transportControl.set_location(TTimeRef(pos.frame, audiodevice().get_sample_rate()));
    m_transportControl.set_realtime(true);
    m_transportControl.set_slave(true);

    m_device->transport_control(&m_transportControl);

    m_runCycleStartTime = TTimeRef::get_nanoseconds_since_epoch();
    m_device->set_transport_cycle_start_time (m_runCycleStartTime);

    m_device->run_cycle( nframes, 0.0);

    m_runCycleEndTime = TTimeRef::get_nanoseconds_since_epoch();
    m_device->set_transport_cycle_end_time (m_runCycleEndTime);

    return 0;
}

// NOTE:  note that in jack2 they (process and sync callback) occur asynchronously in 2 different threads
//        How to handle that properly in Traverso? The TSMP RT event buffer assumes only one RT thread.
int TJackDriver::jack_sync_callback (jack_transport_state_t state, jack_position_t* pos)
{
    m_transportControl.set_state(state);
    m_transportControl.set_location(TTimeRef(pos->frame, audiodevice().get_sample_rate()));
    m_transportControl.set_realtime(true);
    m_transportControl.set_slave(true);
    printf("jack state is %d\n", state);
    printf("jack transport callback, location is %lld\n", m_transportControl.get_location().universal_frame());

    return m_device->transport_control(&m_transportControl);
}

int TJackDriver::_xrun_callback( void * arg )
{
    TJackDriver* driver  = static_cast<TJackDriver *> (arg);
    if (driver->is_running()) {
        driver->m_device->xrun();
    }
    return 0;
}

int TJackDriver::_process_callback (nframes_t nframes, void *arg)
{
    TJackDriver* driver  = static_cast<TJackDriver *> (arg);
    if (!driver->is_running()) {
        return 0;
    }

    return driver->process_callback (nframes);
}

int TJackDriver::_bufsize_callback( nframes_t nframes, void * arg )
{
    TJackDriver* driver  = static_cast<TJackDriver *> (arg);
    driver->m_device->set_buffer_size( nframes );

    emit driver->m_device->driverParamsChanged();
    return 0;
}

void TJackDriver::_on_jack_shutdown_callback( void * arg )
{
    TJackDriver* driver  = static_cast<TJackDriver *> (arg);
    driver->m_running = 2;
}

int TJackDriver::_jack_sync_callback (jack_transport_state_t state, jack_position_t* pos, void* arg)
{
    return static_cast<TJackDriver*> (arg)->jack_sync_callback (state, pos);
}

void TJackDriver::_freewheel_callback(int starting, void *arg)
{
    TJackDriver* driver  = static_cast<TJackDriver *> (arg);

    driver->m_isFreeWheeling = ((starting == 1) ? true : false);
    driver->m_device->driver_changed_free_wheel_mode();

    printf("Jack freewheel callback %d\n", starting);
}

void TJackDriver::start_free_wheeling()
{
    Q_ASSERT(m_jackClient);

    jack_set_freewheel(m_jackClient, 1);
}

void TJackDriver::stop_free_wheeling()
{
    Q_ASSERT(m_jackClient);

    jack_set_freewheel(m_jackClient, 0);
}


float TJackDriver::get_cpu_load( )
{
    return jack_cpu_load(m_jackClient);
}


void TJackDriver::update_config()
{
    m_isSlave = m_device->get_driver_property("jackslave", false).toBool();

    if (m_isSlave) {
        jack_set_sync_callback (m_jackClient, _jack_sync_callback, this);
    } else {
        jack_set_sync_callback(m_jackClient, NULL, this);
    }
}

void TJackDriver::cleanup_removed_port_channel_pair(PortChannelPair* pcpair)
{
    PENTER;
    jack_port_unregister(m_jackClient, pcpair->jackport);

    m_device->delete_channel(pcpair->channel);
    delete pcpair;
    pcpair = nullptr;
}

// Is there a way to get the device name from Jack? Can't find it :-(
// Since Jack uses ALSA, we ask it from ALSA directly :-)
QString TJackDriver::get_device_name( )
{
    return "JACK";
}

QString TJackDriver::get_device_longname( )
{
#if defined (ALSA_SUPPORT)
    return TAlsaDriver::alsa_device_name(true);
#endif
    return "JACK Audio Server";
}


//eof

