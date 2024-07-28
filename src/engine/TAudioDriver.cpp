/*
Copyright (C) 2005-2006 Remon Sijrier

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

$Id: Driver.cpp,v 1.6 2007/03/19 11:18:57 r_sijrier Exp $
*/

#include "TAudioDriver.h"
#include "AudioDevice.h"
#include "AudioChannel.h"

#include <QString>

// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"
#include "qthread.h"


TAudioDriver::TAudioDriver(AudioDevice* device)
    : m_device(device)
    , m_frameRate(0)
    , m_framesPerCycle(0)
{
    read = TAudioDriverReadWriteCallBack(this, &TAudioDriver::_read);
    write = TAudioDriverReadWriteCallBack(this, &TAudioDriver::_write);
    run_cycle = RunCycleCallback(this, &TAudioDriver::_run_cycle);

    m_runCycleStartTime = m_runCycleEndTime = TTimeRef::get_nanoseconds_since_epoch();
}

TAudioDriver::~ TAudioDriver( )
{
	PENTERDES;
        while( ! m_captureChannels.isEmpty())
                m_device->delete_channel(m_captureChannels.takeFirst());

        while( ! m_playbackChannels.isEmpty())
                m_device->delete_channel(m_playbackChannels.takeFirst());
}

int TAudioDriver::_run_cycle( )
{
    m_runCycleEndTime = TTimeRef::get_nanoseconds_since_epoch();

    m_device->set_transport_cycle_end_time (m_runCycleEndTime);

    trav_time_t runCycleTime = (m_runCycleEndTime - m_runCycleStartTime);

    if (m_device->running_real_time()) {
        trav_time_t sleepTime = (m_periodTimeInMicroSeconds * 1000) - runCycleTime;
        QThread::currentThread()->sleep(std::chrono::nanoseconds (sleepTime));
    } else
    {
        // We're free wheeling
        // Limit the amount of runcycles to 50.000 per second.
        // We have to set this limit to not overload the Tsar event queues.

        // 20 microseconds to ryn_cycles() / second == 1.000.000 / 20 = 50.000
        trav_time_t minimumRunCycleTimeInNanoSeconds = (1000 * 20);
        if (runCycleTime < minimumRunCycleTimeInNanoSeconds) {
            QThread::currentThread()->sleep(std::chrono::nanoseconds (minimumRunCycleTimeInNanoSeconds));
        }
    }

    m_runCycleStartTime = TTimeRef::get_nanoseconds_since_epoch();
    m_device->set_transport_cycle_start_time (m_runCycleStartTime);

    return m_device->run_cycle( m_framesPerCycle, 0);
}

int TAudioDriver::_read( nframes_t  )
{
	return 1;
}

int TAudioDriver::_write( nframes_t )
{
        return 1;
}

int TAudioDriver::_null_cycle( nframes_t  )
{
	return 1;
}

int TAudioDriver::attach( )
{
    // int port_flags;
    // port_flags = PortIsOutput|PortIsPhysical|PortIsTerminal;

    AudioChannel* chan;

    m_frameRate = m_device->get_sample_rate();
    m_framesPerCycle = m_device->get_buffer_size();

    m_periodTimeInMicroSeconds = (trav_time_t) floor ((((float) m_framesPerCycle) / m_frameRate) * 1000000.0f);

    m_device->set_buffer_size (m_framesPerCycle);
    m_device->set_sample_rate (m_frameRate);


    // Create 2 capture channels
    for (uint chn=0; chn<2; chn++) {
        chan = add_capture_channel(QString("capture_%1").arg(chn+1));
        chan->set_latency( m_framesPerCycle + m_captureFrameLatency );
    }

    // Create 2 playback channels
    for (uint chn=0; chn<2; chn++) {
        chan = add_playback_channel(QString("playback_%1").arg(chn+1));
        chan->set_latency( m_framesPerCycle + m_captureFrameLatency );
    }

    return 1;
}

AudioChannel* TAudioDriver::add_capture_channel(const QString& chanName)
{
        PENTER;
    AudioChannel* chan = audiodevice().create_channel(chanName, m_captureChannels.size(), AudioChannel::ChannelIsInput);
        m_captureChannels.append(chan);
        return chan;
}

AudioChannel* TAudioDriver::add_playback_channel(const QString& chanName)
{
        PENTER;
        AudioChannel* chan = audiodevice().create_channel(chanName, m_playbackChannels.size(), AudioChannel::ChannelIsOutput);
        m_playbackChannels.append(chan);
        return chan;
}


AudioChannel* TAudioDriver::get_capture_channel_by_name(const QString& name)
{
        foreach(AudioChannel* chan, m_captureChannels) {
                if (chan->get_name() == name) {
                        return chan;
                }
        }
        return nullptr;
}


AudioChannel* TAudioDriver::get_playback_channel_by_name(const QString& name)
{
        foreach(AudioChannel* chan, m_playbackChannels) {
                if (chan->get_name() == name) {
                        return chan;
                }
        }
        return nullptr;
}



int TAudioDriver::detach( )
{
	return 0;
}

int TAudioDriver::start( )
{
    emit driverSetupMessage(tr("Succesfully started Dummy Driver!"), AudioDevice::DRIVER_SETUP_SUCCESS);
    return 1;
}

int TAudioDriver::stop( )
{
	return 1;
}

QString TAudioDriver::get_device_name( )
{
	return "Null Audio Device";
}

QString TAudioDriver::get_device_longname( )
{
	return "Null Audio Device";
}


//eof
