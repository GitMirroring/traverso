/*
    Copyright (C) 2005-2019 Remon Sijrier

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

#include "AudioChannel.h"

#include "TVUMonitor.h"
#include "TAudioThreadMessageQueue.h"
#include "Utils.h"
#include "Debugger.h"

#include <QString>


/**
 * \class AudioChannel
 * \brief AudioChannel wraps one 'real' audiochannel into an easy to use class
 *
 * An AudioChannel has a audio_sample_t* buffer, a name and some functions for setting the buffer size, 
 * and monitoring the highest peak value, which is handy to use by for example a VU meter. 
 */


AudioChannel::AudioChannel(const QString& name, uint channelNumber, int type, nframes_t bufferSize, qint64 id)
    : m_audioBuffer(bufferSize)
{
    m_name = name;
    m_number = channelNumber;
    m_type = type;
    m_monitoring = true;
    m_latency = 0;
    if (id == 0) {
        m_id = TraversoDAW::Utils::create_id();
    } else {
        m_id = id;
    }
}

AudioChannel::~ AudioChannel( )
{
    PENTERDES2;
}

void AudioChannel::set_latency( uint latency )
{
    m_latency = latency;
}

void AudioChannel::set_buffer_size( nframes_t size )
{
    m_audioBuffer.resize(size);
}


void AudioChannel::process_monitoring(TVUMonitor* monitor)
{
    float peakValue = m_audioBuffer.compute_peak();

    if (monitor) {
        monitor->process(peakValue);
    }

    for(TVUMonitor* internalMonitor = m_monitors.first(); internalMonitor != nullptr; internalMonitor = internalMonitor->next) {
        internalMonitor->process(peakValue);
    }
}

void AudioChannel::set_monitoring( bool monitor )
{
    m_monitoring = monitor;
}


void AudioChannel::private_add_monitor(TVUMonitor *monitor)
{
    m_monitors.append(monitor);
}

void AudioChannel::private_remove_monitor(TVUMonitor *monitor)
{
    if (!m_monitors.remove(monitor)) {
        printf("AudioChannel:: VUMonitor was not in monitors list, failed to remove it!\n");
    }
}

void AudioChannel::add_monitor(TVUMonitor *monitor)
{
    tsmp().post_gui_event(this, monitor, "private_add_monitor(TVUMonitor*)", "vuMonitorAdded(TVUMonitor*)");
}

void AudioChannel::remove_monitor(TVUMonitor *monitor)
{
    tsmp().post_gui_event(this, monitor, "private_remove_monitor(TVUMonitor*)", "vuMonitorAdded(TVUMonitor*)");
}

void AudioChannel::read_from_hardware_port(audio_sample_t *buf, nframes_t nframes)
{
    memcpy (m_audioBuffer.get_data(nframes), buf, sizeof(audio_sample_t) * nframes);

    if (m_monitoring) {
        process_monitoring();
    }
}

void AudioChannel::read_from_hardware_port_interleaved(const audio_sample_t *buf, nframes_t nframes, uint channelCount, uint channelNumber)
{
    audio_sample_t *dst = m_audioBuffer.get_data(nframes);
    for (nframes_t frame = 0; frame < nframes; ++frame) {
        dst[frame] = buf[frame * channelCount + channelNumber];
    }

    if (m_monitoring) {
        process_monitoring();
    }
}


//eof
