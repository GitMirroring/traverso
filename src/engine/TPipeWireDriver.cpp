/*
    Copyright (C) 2026 Remon Sijrier, Ben Levitt

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


#include "TPipeWireDriver.h"

#if defined (PIPEWIRE_SUPPORT)

#include "TAudioDevice.h"
#include "AudioChannel.h"
#include "TTimeRef.h"
#include "Debugger.h"

#include <cstring>


TPipeWireDriver::TPipeWireDriver(TAudioDevice* device)
    : TAudioDriver(device)
{
    read = TAudioDriverReadWriteCallBack::from_method<TPipeWireDriver, &TPipeWireDriver::_read>(this);
    write = TAudioDriverReadWriteCallBack::from_method<TPipeWireDriver, &TPipeWireDriver::_write>(this);
    run_cycle = TRunCycleCallBack::from_method<TPipeWireDriver, &TPipeWireDriver::_run_cycle>(this);
}

TPipeWireDriver::~TPipeWireDriver()
{
    PENTERDES;

}

int TPipeWireDriver::setup_failed(const QString& message)
{
    PENTER;
    emit driverSetupMessage("PipeWire", message, TAudioDevice::DRIVER_SETUP_FAILURE);
    return -1;
}


int TPipeWireDriver::setup(bool capture, bool playback, const QString& cardDevice)
{
    PENTER;

    m_frameRate = m_device->get_sample_rate();
    m_framesPerCycle = m_device->get_buffer_size();
    m_captureFrameLatency = m_playbackFrameLatency = 0;

    // FIXME:
    uint32_t channels = 2;

    m_periodTimeInMicroSeconds = static_cast<trav_time_t>(
        static_cast<double>(m_framesPerCycle) / m_frameRate * 1000000.0);

    pw_init(nullptr, nullptr);

    m_pwLoop = pw_loop_new(nullptr);

    if (!m_pwLoop) {
        return setup_failed(tr("Could not create PipeWire Main Loop"));
    }

    struct pw_properties *playbackProperties = pw_properties_new(
        "application.name", "Traverso DAW",
        "application.icon-name", "Traverso",
        "media.name", "Traverso Audio Output",
        "media.type", "Audio",
        "media.category", "Playback",
        "media.class", "Stream/Output/Audio",
        "node.name", "TraversoDAW Playback",
        "node.description", "Traverso DAW Playback",

        "node.link-group", "Traverso_DSP_Group",

        "node.force-quantum", std::to_string(m_framesPerCycle).c_str(),
        "node.force-rate", std::to_string(m_frameRate).c_str(),
        "node.lock-quantum", "true",
        "node.lock-rate", "true",
        nullptr
        );

    std::memset(&m_playbackStreamEvents, 0, sizeof(m_playbackStreamEvents));
    m_playbackStreamEvents.version = PW_VERSION_STREAM_EVENTS;
    m_playbackStreamEvents.process = &TPipeWireDriver::_on_process_playback;
    m_playbackStreamEvents.state_changed = &TPipeWireDriver::_on_state_changed;

    m_playbackStream = pw_stream_new_simple(
        m_pwLoop,
        "TraversoPlaybackStream",
        playbackProperties,
        &m_playbackStreamEvents,
        this
        );

    if (!m_playbackStream) {
        return setup_failed(tr("Could not create PipeWire Playback Stream"));
    }

    uint8_t paramBuffer[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(paramBuffer, sizeof(paramBuffer));
    struct spa_audio_info_raw info = {};
    info.format   = SPA_AUDIO_FORMAT_F32P; // Float 32-bit PLANAR (JACK stijl)
    info.rate     = m_frameRate;
    info.channels = channels;
    info.flags    = 0;

    for (uint32_t i = 0; i < channels; ++i) {
        info.position[i] = (i == 0) ? SPA_AUDIO_CHANNEL_FL : ((i == 1) ? SPA_AUDIO_CHANNEL_FR : SPA_AUDIO_CHANNEL_UNKNOWN);
    }

    const struct spa_pod *duplexParameter = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info);
    const struct spa_pod *streamParameters[] = { duplexParameter };

    // Verbind de Playback stream met de Graph
    int res = pw_stream_connect(
        m_playbackStream,
        PW_DIRECTION_OUTPUT,
        PW_ID_ANY,
        static_cast<enum pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_RT_PROCESS),
        streamParameters,
        1
        );

    if (res < 0) {
        return setup_failed(tr("Could not connect playback stream to server"));
    } else {
        emit driverSetupMessage("PipeWire", tr("Playback Stream connected to server"), TAudioDevice::DRIVER_SETUP_SUCCESS);
    }

    struct pw_properties *captureProps = pw_properties_new(
        "application.name", "Traverso DAW",
        "application.icon-name", "Traverso",
        "media.name", "Traverso Audio Input",
        "media.type", "Audio",
        "media.category", "Capture",
        "media.class", "Stream/Input/Audio",
        "node.name", "TraversoDAW Capture",
        "node.description", "Traverso DAW Input",

        "node.link-group", "Traverso_DSP_Group",

        "node.force-quantum", std::to_string(m_framesPerCycle).c_str(),
        "node.force-rate", std::to_string(m_frameRate).c_str(),
        "node.lock-quantum", "true",
        "node.lock-rate", "true",
        nullptr
        );

    std::memset(&m_captureStreamEvents, 0, sizeof(m_captureStreamEvents));
    m_captureStreamEvents.version = PW_VERSION_STREAM_EVENTS;

    m_captureStreamEvents.process = &TPipeWireDriver::_on_process_capture;
    m_captureStreamEvents.state_changed = &TPipeWireDriver::_on_state_changed;

    m_captureStream = pw_stream_new_simple(
        m_pwLoop,
        "TraversoCaptureStream",
        captureProps,
        &m_captureStreamEvents,
        this
        );

    if (!m_captureStream) {
        return setup_failed(tr("Could not create PipeWire Capture Stream"));
    }

    res = pw_stream_connect(
        m_captureStream,
        PW_DIRECTION_INPUT,
        PW_ID_ANY,
        static_cast<enum pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_RT_PROCESS),
        streamParameters,
        1
        );

    if (res < 0) {
        return setup_failed(tr("Could not connect capture stream to server"));
    }

    int pipewire_fd = pw_loop_get_fd(m_pwLoop);
    m_notifier = new QSocketNotifier(pipewire_fd, QSocketNotifier::Read, this);
    m_notifier->setEnabled(false);

    connect(m_notifier, &QSocketNotifier::activated, this, &TPipeWireDriver::handle_pipewire_events);

    return 1;
}

int TPipeWireDriver::attach()
{
    // int port_flags;
    // port_flags = PortIsOutput|PortIsPhysical|PortIsTerminal;

    m_periodTimeInMicroSeconds = (trav_time_t) floor ((((float) m_framesPerCycle) / m_frameRate) * 1000000.0f);

    m_device->set_buffer_size (m_framesPerCycle);
    m_device->set_sample_rate (m_frameRate);


    AudioChannel* chan;

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

int TPipeWireDriver::start()
{
    PENTER;
    Q_ASSERT(m_playbackStream);


    if (m_notifier) {
        m_notifier->setEnabled(true);
    }


    return 1;
}

int TPipeWireDriver::stop()
{
    PENTER;

    if (!m_pwLoop) return 1;

    if (m_notifier) {
        m_notifier->setEnabled(false);
        disconnect(m_notifier, &QSocketNotifier::activated, this, &TPipeWireDriver::handle_pipewire_events);
        m_notifier->deleteLater();
        m_notifier = nullptr;
    }
    if (m_playbackStream) {
        pw_stream_destroy(m_playbackStream);
        m_playbackStream = nullptr;
    }
    if (m_captureStream) {
        pw_stream_destroy(m_captureStream);
        m_captureStream = nullptr;
    }
    if (m_pwLoop) {
        pw_loop_destroy(m_pwLoop);
        m_pwLoop = nullptr;
    }
    pw_deinit();

    return 1;
}

// Called in RT thread from PipeWire server
int TPipeWireDriver::process_callback()
{
    m_runCycleStartTime = TTimeRef::get_nanoseconds_since_epoch();
    m_device->set_transport_cycle_start_time(m_runCycleStartTime);

    int64_t newPlaybackLatency = m_playbackFrameLatency;
    int64_t newCaptureLatency = m_captureFrameLatency;

    if (m_playbackStream) {
        struct pw_time playback_time;
        if (pw_stream_get_time_n(m_playbackStream, &playback_time, sizeof(playback_time)) == 0) {
            int64_t out_delay = playback_time.delay + playback_time.queued + playback_time.buffered;
            if (out_delay < 0) {
                out_delay = playback_time.size;
            }
            newPlaybackLatency = out_delay;
        }
    }

    if (m_captureStream) {
        struct pw_time capture_time;
        if (pw_stream_get_time_n(m_captureStream, &capture_time, sizeof(capture_time)) == 0) {
            int64_t in_delay = capture_time.delay + capture_time.queued + capture_time.buffered;
            if (in_delay < 0) {
                in_delay = capture_time.size;
            }
            newCaptureLatency = in_delay;
        }
    }

    if (newPlaybackLatency != m_playbackFrameLatency || newCaptureLatency != m_captureFrameLatency) {
        m_playbackFrameLatency = newPlaybackLatency;
        m_captureFrameLatency = newCaptureLatency;

        tsmp().post_rt_event(m_latencyChangedEvent);
    }

    m_device->run_cycle(m_framesPerCycle, 0.0);

    m_runCycleEndTime = TTimeRef::get_nanoseconds_since_epoch();
    m_device->set_transport_cycle_end_time(m_runCycleEndTime);

    return 1;
}

// Called in RT thread from TAudioDevice
int TPipeWireDriver::_read( nframes_t nframes )
{
    // allready got data in _on_process_capture() callback
    return 1;
}

// Called in RT thread from TAudioDevice
int TPipeWireDriver::_write( nframes_t nframes )
{
    struct pw_buffer* b = pw_stream_dequeue_buffer(m_playbackStream);
    if (!b) return -1;

    struct spa_buffer* buf = b->buffer;
    uint channelCount = m_playbackChannels.size();

    for (uint chan = 0; chan < channelCount; ++chan) {
        float* dst = static_cast<float*>(buf->datas[chan].data);

        if (dst) {
            std::memcpy(dst, m_playbackChannels.at(chan)->get_buffer().get_data(0), nframes * sizeof(float));
        }

        m_playbackChannels.at(chan)->silence_buffer();

        if (buf->datas[chan].chunk) {
            buf->datas[chan].chunk->offset = 0;
            buf->datas[chan].chunk->stride = sizeof(float);
            buf->datas[chan].chunk->size = nframes * sizeof(float);
        }
    }

    pw_stream_queue_buffer(m_playbackStream, b);
    return 1;
}


// Called in RT thread
int TPipeWireDriver::_run_cycle()
{
    return m_device->run_cycle(m_framesPerCycle, 0);
}

// Called in RT thread from PipeWire server
void TPipeWireDriver::_on_process_playback(void *userdata)
{
    TPipeWireDriver* driver  = static_cast<TPipeWireDriver *> (userdata);

    driver->process_callback();
}

// Called in RT thread from PipeWire server
void TPipeWireDriver::_on_process_capture(void *userdata)
{
    TPipeWireDriver* driver = static_cast<TPipeWireDriver*>(userdata);
    driver->process_capture_callback();
}

// Called in RT thread from PipeWire server
int TPipeWireDriver::process_capture_callback()
{
    struct pw_buffer* b = pw_stream_dequeue_buffer(m_captureStream);
    if (!b) return 0;

    struct spa_buffer* buf = b->buffer;
    uint channelCount = m_captureChannels.size();

    for (uint chan = 0; chan < channelCount; ++chan) {
        if (chan < buf->n_datas && buf->datas[chan].data) {
            float* src = static_cast<float*>(buf->datas[chan].data);
            m_captureChannels.at(chan)->read_from_hardware_port(src, m_framesPerCycle);
        }
    }

    pw_stream_queue_buffer(m_captureStream, b);
    return 1;
}

void TPipeWireDriver::_on_state_changed(void *userdata, enum pw_stream_state old_state, enum pw_stream_state state, const char *error) {
    static_cast<TPipeWireDriver*>(userdata)->handle_state_changed(old_state, state, error);
}

void TPipeWireDriver::handle_state_changed(enum pw_stream_state old_state, enum pw_stream_state state, const char *error)
{
    PENTER;
    // emit driverStateChanged(static_cast<int>(state));

    if (state == PW_STREAM_STATE_ERROR && error) {
        std::cerr << "Stream Error: " << error << std::endl;
    }
    if (state == PW_STREAM_STATE_CONNECTING) {
        std::cerr << "Stream Connecting" << std::endl;
    }
    if (state == PW_STREAM_STATE_PAUSED) {
        std::cerr << "Stream Paused" << std::endl;
    }
    if (state == PW_STREAM_STATE_STREAMING) {
        std::cout << "Stream running" << std::endl;
    }
}

void TPipeWireDriver::handle_pipewire_events()
{
    PENTER;
    pw_loop_iterate(m_pwLoop, 0);
}

QString TPipeWireDriver::get_device_name()
{
    return "PipeWire";
}

QString TPipeWireDriver::get_device_longname()
{
    return "PipeWire Audio Server";
}

#endif // PIPEWIRE_SUPPORT
