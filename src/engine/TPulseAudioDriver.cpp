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

#include "TAudioDevice.h"
#include "AudioChannel.h"



#include "Debugger.h"

TPulseAudioDriver::TPulseAudioDriver(TAudioDevice* device )
    : TAudioDriver(device)
    , m_interleavedPlaybackBuffer(device->get_buffer_size() * 2)
    , m_interleavedCaptureBuffer(device->get_buffer_size() * 2)
{
    read = TAudioDriverReadWriteCallBack(this, &TPulseAudioDriver::_read);
    write = TAudioDriverReadWriteCallBack(this, &TPulseAudioDriver::_write);
    run_cycle = RunCycleCallback(this, &TPulseAudioDriver::_run_cycle);

    m_paSimplePlayback = nullptr;
    m_paSimpleCapture = nullptr;
}

TPulseAudioDriver::~TPulseAudioDriver( )
{
    if (m_paSimplePlayback) {
        pa_simple_free(m_paSimplePlayback);
    }
    if (m_paSimpleCapture) {
        pa_simple_free(m_paSimpleCapture);
    }
}

int TPulseAudioDriver::_read( nframes_t /*nframes*/ )
{
    // if (!m_paSimpleCapture) {
    //     return 1;
    // }

    // Q_ASSERT(m_captureChannels.size());

    // m_device->set_transport_cycle_start_time(TTimeRef::get_nanoseconds_since_epoch());

    // int error;

    // if (pa_simple_read(m_paSimpleCapture, m_interleavedCaptureBuffer, m_framesPerCycle * sizeof(audio_sample_t) * 2, &error) < 0) {
    //     fprintf(stderr, __FILE__": pa_simple_read() failed: %s\n", pa_strerror(error));
    // }

    // auto leftChannelBuffer = m_captureChannels.at(0)->get_buffer(nframes);
    // auto rightChannelBuffer = m_captureChannels.at(1)->get_buffer(nframes);

    // for (uint x = 0; x < nframes; ++x) {
    //     leftChannelBuffer[x] = m_interleavedCaptureBuffer[x*2];
    //     rightChannelBuffer[x] = m_interleavedCaptureBuffer[1+(x*2)];
    // }

    return 1;
}

int TPulseAudioDriver::_write( nframes_t nframes )
{
    if (!m_paSimplePlayback) {
        return 1;
    }

    Q_ASSERT(m_playbackChannels.size());

    int error;

    auto leftChannelBuffer = m_playbackChannels.at(0)->get_buffer().get_data(nframes);
    auto rightChannelBuffer = m_playbackChannels.at(1)->get_buffer().get_data(nframes);

    for (uint x = 0; x < nframes; ++x) {
        m_interleavedPlaybackBuffer[x*2] = leftChannelBuffer[x];
        m_interleavedPlaybackBuffer[1+(x*2)] = rightChannelBuffer[x];
    }

    m_device->set_transport_cycle_end_time(TTimeRef::get_nanoseconds_since_epoch());

    if (pa_simple_write(m_paSimplePlayback, m_interleavedPlaybackBuffer.get_data(nframes), m_framesPerCycle * sizeof(audio_sample_t) * 2, &error) < 0) {
        fprintf(stderr, __FILE__": pa_simple_write() failed: %s\n", pa_strerror(error));
    }

    for (auto channel : m_playbackChannels) {
        channel->silence_buffer();
    }

    m_device->set_transport_cycle_start_time(TTimeRef::get_nanoseconds_since_epoch());

    return 1;
}

int TPulseAudioDriver::setup(bool /*capture*/, bool playback, const QString& )
{
	PENTER;
    int error;

    m_frameRate = m_device->get_sample_rate();
    m_framesPerCycle = m_device->get_buffer_size();
	
    m_sampleSpec.rate = m_frameRate;
    m_sampleSpec.channels = 2;
    m_sampleSpec.format = PA_SAMPLE_FLOAT32;

    if (playback) {
        m_paSimplePlayback = pa_simple_new(NULL, "Traverso_Playback", PA_STREAM_PLAYBACK, NULL, "playback", &m_sampleSpec, NULL, NULL, &error);

        if (!m_paSimplePlayback) {
            emit driverSetupMessage("PulseAudio", tr("Unable to connect to PulseAudio server for Playback stream!"), TAudioDevice::DRIVER_SETUP_FAILURE);
            return -1;
        }

        m_interleavedPlaybackBuffer.resize(m_framesPerCycle*m_sampleSpec.channels);
    }

    // if (capture) {
    //     m_paSimpleCapture = pa_simple_new(NULL, "Traverso_Capture", PA_STREAM_RECORD, NULL, "record", &m_sampleSpec, NULL, NULL, &error);

    //     if (!m_paSimpleCapture) {
    //         emit driverSetupMessage("PulseAudio", tr("Unable to connect to PulseAudio server for Record stream!"), TAudioDevice::DRIVER_SETUP_FAILURE);
    //         return -1;
    //     }

    //     m_interleavedCaptureBuffer.resize(m_framesPerCycle*m_sampleSpec.channels);
    // }

    emit driverSetupMessage("PulseAudio", tr("Succesfully connected to PulseAudio server!"), TAudioDevice::DRIVER_SETUP_SUCCESS);

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

    // silences the playback buffers
    TAudioDriver::start();

    return 1;
}

int TPulseAudioDriver::stop( )
{
	PENTER;
    int error;
    pa_simple_flush(m_paSimplePlayback, &error);
    // pa_simple_flush(m_paSimpleCapture, &error);

    // silence capture channels
    TAudioDriver::stop();

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

