/*
    Copyright (C) 2008 Remon Sijrier 
 
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

#include "TPulseAudioDriver.h"

#include <pulse/error.h>

#include "AudioDevice.h"
#include "AudioChannel.h"

// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"

TPulseAudioDriver::TPulseAudioDriver(AudioDevice* device )
    : TAudioDriver(device)
{
    read = MakeDelegate(this, &TPulseAudioDriver::_read);
    write = MakeDelegate(this, &TPulseAudioDriver::_write);
    run_cycle = RunCycleCallback(this, &TPulseAudioDriver::_run_cycle);

    m_paSimple = nullptr;
    m_interleavedBuffer = nullptr;
}

TPulseAudioDriver::~TPulseAudioDriver( )
{
}

int TPulseAudioDriver::_read( nframes_t nframes )
{
    Q_ASSERT(m_paSimple);

    int error;

    // if (pa_simple_read(m_paSimple, m_interleavedBuffer, m_framesPerCycle * sizeof(audio_sample_t) * 2, &error) < 0) {
    //     fprintf(stderr, __FILE__": pa_simple_read() failed: %s\n", pa_strerror(error));
    // }

    return 1;
}

int TPulseAudioDriver::_write( nframes_t nframes )
{
    Q_ASSERT(m_paSimple);

    int error;

    if (m_playbackChannels.size() > 0) {
    } else {
        printf("No playback channels\n");
        return 0;
    }

    auto decodebuffer0 = m_playbackChannels.at(0)->get_buffer(nframes);
    auto decodebuffer1 = m_playbackChannels.at(1)->get_buffer(nframes);

    for (uint x = 0; x < nframes; ++x) {
        m_interleavedBuffer[x*2] = decodebuffer0[x];
        m_interleavedBuffer[1+(x*2)] = decodebuffer1[x];
    }

    m_device->transport_cycle_end(get_microseconds());

    if (pa_simple_write(m_paSimple, m_interleavedBuffer, m_framesPerCycle * sizeof(audio_sample_t) * 2, &error) < 0) {
        fprintf(stderr, __FILE__": pa_simple_write() failed: %s\n", pa_strerror(error));
    }

    for (auto channel : m_playbackChannels) {
        channel->silence_buffer(m_framesPerCycle);
    }

    m_device->transport_cycle_start(get_microseconds());

    return 1;
}

int TPulseAudioDriver::setup(bool capture, bool playback, const QString& )
{
	PENTER;
    int error;

    m_frameRate = m_device->get_sample_rate();
    m_framesPerCycle = m_device->get_buffer_size();
	
    m_sampleSpec.rate = m_frameRate;
    m_sampleSpec.channels = 2;
    m_sampleSpec.format = PA_SAMPLE_FLOAT32;

    if (m_interleavedBuffer) {
        delete [] m_interleavedBuffer;
    }
    m_interleavedBuffer = new audio_sample_t[m_framesPerCycle*m_sampleSpec.channels];

    m_paSimple = pa_simple_new(NULL, "Traverso", PA_STREAM_PLAYBACK, NULL, "playback", &m_sampleSpec, NULL, NULL, &error);

    if (!m_paSimple) {
        m_device->driverSetupMessage(tr("Unable to connect to PulseAudio server!"), AudioDevice::DRIVER_SETUP_FAILURE);
        return -1;
    }

    m_device->driverSetupMessage(tr("Succesfully connected to PulseAudio server!"), AudioDevice::DRIVER_SETUP_SUCCESS);

    return 1;
}

int TPulseAudioDriver::attach( )
{
    PENTER;

    return TAudioDriver::attach();
}

int TPulseAudioDriver::start( )
{
    PENTER;
    return 1;
}

int TPulseAudioDriver::stop( )
{
	PENTER;

    pa_simple_free(m_paSimple);

	return 1;
}

QString TPulseAudioDriver::get_device_name()
{
	return "Pulse";
}

QString TPulseAudioDriver::get_device_longname()
{
	return "Pulse";
}

int TPulseAudioDriver::_run_cycle()
{
    return m_device->run_cycle(m_framesPerCycle, 0);
}

