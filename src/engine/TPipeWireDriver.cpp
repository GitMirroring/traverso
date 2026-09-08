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

#include "Utils.h"
#include "TAudioDevice.h"
#include "AudioChannel.h"
#include "TTimeRef.h"
#include "Debugger.h"

#include <cstring>
#include <algorithm>

TPipeWireDriver::TPipeWireDriver(TAudioDevice* device)
    : TAudioDriver(device)
{
    m_running.store(0);
    m_threadLoop = nullptr;
    m_playbackStream = nullptr;
    m_captureStream = nullptr;
    m_ioPosition = nullptr;
    m_isSlave = false;
    m_cpuLoad.store(0.0f);
}

TPipeWireDriver::~TPipeWireDriver()
{
    PENTER;
    stop();

    if (m_threadLoop) {
        pw_thread_loop_stop(m_threadLoop);
    }

    if (m_playbackStream) {
        pw_stream_destroy(m_playbackStream);
        m_playbackStream = nullptr;
    }

    if (m_captureStream) {
        pw_stream_destroy(m_captureStream);
        m_captureStream = nullptr;
    }

    if (m_threadLoop) {
        pw_thread_loop_destroy(m_threadLoop);
        m_threadLoop = nullptr;
    }

    pw_deinit();
}

int TPipeWireDriver::_read(nframes_t)
{
    return 1;
}

int TPipeWireDriver::_write(nframes_t)
{
    return 1;
}

int TPipeWireDriver::fail_setup(const QString& message)
{
    if (m_threadLoop) {
        pw_thread_loop_stop(m_threadLoop);
    }
    if (m_playbackStream) {
        pw_stream_destroy(m_playbackStream);
        m_playbackStream = nullptr;
    }
    if (m_captureStream) {
        pw_stream_destroy(m_captureStream);
        m_captureStream = nullptr;
    }
    if (m_threadLoop) {
        pw_thread_loop_destroy(m_threadLoop);
        m_threadLoop = nullptr;
    }
    pw_deinit();

    emit driverSetupMessage("PipeWire", message, TAudioDevice::DRIVER_SETUP_FAILURE);
    return -1;
}

int TPipeWireDriver::setup(bool capture, bool playback, const QString& cardDevice)
{
    PENTER;

    m_enableCapture = capture;
    m_enablePlayback = playback;
    m_frameRate = m_device->get_sample_rate();
    m_framesPerCycle = m_device->get_buffer_size();
    if (m_frameRate == 0) m_frameRate = 48000;
    if (m_framesPerCycle == 0) m_framesPerCycle = 1024;
    m_cardDevice = cardDevice;
    m_threadLoop = nullptr;
    m_playbackStream = nullptr;
    m_captureStream = nullptr;
    m_ioPosition = nullptr;
    m_captureFrameLatency = m_playbackFrameLatency = 0;

    m_device->set_buffer_size(m_framesPerCycle);
    m_device->set_sample_rate(m_frameRate);
    m_periodTimeInMicroSeconds = static_cast<trav_time_t>(
        static_cast<double>(m_framesPerCycle) / m_frameRate * 1000000.0);

    if (m_enableCapture) {
        AudioChannel* chan1 = add_capture_channel("capture_1");
        chan1->set_latency(m_framesPerCycle + m_captureFrameLatency);
        AudioChannel* chan2 = add_capture_channel("capture_2");
        chan2->set_latency(m_framesPerCycle + m_captureFrameLatency);
    }

    if (m_enablePlayback) {
        AudioChannel* chan1 = add_playback_channel("playback_1");
        chan1->set_latency(m_framesPerCycle + m_playbackFrameLatency);
        AudioChannel* chan2 = add_playback_channel("playback_2");
        chan2->set_latency(m_framesPerCycle + m_playbackFrameLatency);
    }

    if (m_enableCapture && !m_captureChannels.isEmpty()) {
        m_inputRingBuffer = std::make_unique<RingBufferNPT<audio_sample_t>>(m_framesPerCycle * m_captureChannels.size() * 16);
        m_interleavedInputBuffer.resize(m_framesPerCycle * m_captureChannels.size() * 4);
    }

    if (m_enablePlayback && !m_playbackChannels.isEmpty()) {
        m_outputRingBuffer = std::make_unique<RingBufferNPT<audio_sample_t>>(m_framesPerCycle * m_playbackChannels.size() * 16);
        m_interleavedOutputBuffer.resize(m_framesPerCycle * m_playbackChannels.size() * 4);
    }

    printf("Connecting to the PipeWire server...\n");

    pw_init(nullptr, nullptr);

    m_threadLoop = pw_thread_loop_new("traverso-pipewire", nullptr);
    if (!m_threadLoop) {
        return fail_setup(tr("Couldn't create PipeWire thread loop"));
    }

    m_playbackEvents = {};
    m_playbackEvents.version = PW_VERSION_STREAM_EVENTS;
    m_playbackEvents.destroy = _on_playback_destroy;
    m_playbackEvents.state_changed = _on_playback_state_changed;
    m_playbackEvents.io_changed = _on_io_changed;
    m_playbackEvents.param_changed = _on_param_changed;
    m_playbackEvents.process = _on_playback_process;

    m_captureEvents = {};
    m_captureEvents.version = PW_VERSION_STREAM_EVENTS;
    m_captureEvents.destroy = _on_capture_destroy;
    m_captureEvents.state_changed = _on_capture_state_changed;
    m_captureEvents.io_changed = _on_io_changed;
    m_captureEvents.param_changed = _on_param_changed;
    m_captureEvents.process = _on_capture_process;

    QByteArray latencyStr = QString("%1/%2").arg(m_framesPerCycle).arg(m_frameRate).toUtf8();
    QByteArray rateStr = QString("1/%1").arg(m_frameRate).toUtf8();

    pw_thread_loop_lock(m_threadLoop);

    enum pw_stream_flags flags = static_cast<enum pw_stream_flags>(
        PW_STREAM_FLAG_AUTOCONNECT |
        PW_STREAM_FLAG_MAP_BUFFERS |
        PW_STREAM_FLAG_RT_PROCESS |
        PW_STREAM_FLAG_INACTIVE
    );

    if (m_enablePlayback) {
        struct pw_properties* props = pw_properties_new(
            PW_KEY_MEDIA_TYPE, "Audio",
            PW_KEY_MEDIA_CATEGORY, "Playback",
            PW_KEY_MEDIA_ROLE, "Production",
            PW_KEY_APP_NAME, "Traverso",
            PW_KEY_NODE_NAME, "Traverso",
            PW_KEY_NODE_DESCRIPTION, "Traverso DAW",
            PW_KEY_NODE_LATENCY, latencyStr.constData(),
            PW_KEY_NODE_RATE, rateStr.constData(),
            "node.lock-quantum", "true",
            (const char*)nullptr
        );
        if (!m_cardDevice.isEmpty()) {
            pw_properties_set(props, PW_KEY_TARGET_OBJECT, m_cardDevice.toUtf8().constData());
        }

        m_playbackStream = pw_stream_new_simple(
            pw_thread_loop_get_loop(m_threadLoop),
            "Traverso Playback",
            props,
            &m_playbackEvents,
            this
        );

        if (!m_playbackStream) {
            pw_thread_loop_unlock(m_threadLoop);
            return fail_setup(tr("Couldn't create PipeWire playback stream"));
        }

        uint8_t buffer[1024];
        struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
        struct spa_audio_info_raw info = {};
        info.format = SPA_AUDIO_FORMAT_F32;
        info.channels = static_cast<uint32_t>(m_playbackChannels.size());
        if (info.channels == 1) {
            info.position[0] = SPA_AUDIO_CHANNEL_MONO;
        } else if (info.channels >= 2) {
            info.position[0] = SPA_AUDIO_CHANNEL_FL;
            info.position[1] = SPA_AUDIO_CHANNEL_FR;
        }
        info.rate = m_frameRate;

        const struct spa_pod* params[1];
        params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info);

        int res = pw_stream_connect(
            m_playbackStream,
            PW_DIRECTION_OUTPUT,
            PW_ID_ANY,
            flags,
            params,
            1
        );

        if (res < 0) {
            pw_thread_loop_unlock(m_threadLoop);
            return fail_setup(tr("Failed to connect PipeWire playback stream"));
        }
    }

    if (m_enableCapture) {
        struct pw_properties* props = pw_properties_new(
            PW_KEY_MEDIA_TYPE, "Audio",
            PW_KEY_MEDIA_CATEGORY, "Capture",
            PW_KEY_MEDIA_ROLE, "Production",
            PW_KEY_APP_NAME, "Traverso",
            PW_KEY_NODE_NAME, "Traverso Capture",
            PW_KEY_NODE_DESCRIPTION, "Traverso DAW Capture",
            PW_KEY_NODE_LATENCY, latencyStr.constData(),
            PW_KEY_NODE_RATE, rateStr.constData(),
            (const char*)nullptr
        );
        if (!m_cardDevice.isEmpty()) {
            pw_properties_set(props, PW_KEY_TARGET_OBJECT, m_cardDevice.toUtf8().constData());
        }

        m_captureStream = pw_stream_new_simple(
            pw_thread_loop_get_loop(m_threadLoop),
            "Traverso Capture",
            props,
            &m_captureEvents,
            this
        );

        if (!m_captureStream) {
            pw_thread_loop_unlock(m_threadLoop);
            return fail_setup(tr("Couldn't create PipeWire capture stream"));
        }

        uint8_t buffer[1024];
        struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
        struct spa_audio_info_raw info = {};
        info.format = SPA_AUDIO_FORMAT_F32;
        info.channels = static_cast<uint32_t>(m_captureChannels.size());
        if (info.channels == 1) {
            info.position[0] = SPA_AUDIO_CHANNEL_MONO;
        } else if (info.channels >= 2) {
            info.position[0] = SPA_AUDIO_CHANNEL_FL;
            info.position[1] = SPA_AUDIO_CHANNEL_FR;
        }
        info.rate = m_frameRate;

        const struct spa_pod* params[1];
        params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info);

        int res = pw_stream_connect(
            m_captureStream,
            PW_DIRECTION_INPUT,
            PW_ID_ANY,
            flags,
            params,
            1
        );

        if (res < 0) {
            pw_thread_loop_unlock(m_threadLoop);
            return fail_setup(tr("Failed to connect PipeWire capture stream"));
        }
    }

    pw_thread_loop_unlock(m_threadLoop);

    if (pw_thread_loop_start(m_threadLoop) < 0) {
        return fail_setup(tr("Failed to start PipeWire thread loop"));
    }

    update_config();

    emit driverSetupMessage("PipeWire", tr("Successfully connected to PipeWire server!"), TAudioDevice::DRIVER_SETUP_SUCCESS);

    return 1;
}

int TPipeWireDriver::attach()
{
    m_device->set_buffer_size(m_framesPerCycle);
    m_device->set_sample_rate(m_frameRate);
    return 1;
}

int TPipeWireDriver::start()
{
    PENTER;
    if (!m_threadLoop) {
        return -1;
    }

    // silence playback buffers
    TAudioDriver::start();

    m_running.store(1);

    if (m_inputRingBuffer) {
        m_inputRingBuffer->reset();
    }
    if (m_outputRingBuffer) {
        m_outputRingBuffer->reset();
    }

    pw_thread_loop_lock(m_threadLoop);
    if (m_playbackStream) {
        pw_stream_set_active(m_playbackStream, true);
    }
    if (m_captureStream) {
        pw_stream_set_active(m_captureStream, true);
    }
    pw_thread_loop_unlock(m_threadLoop);

    emit driverSetupMessage("PipeWire", tr("Successfully connected to PipeWire server!"), TAudioDevice::DRIVER_SETUP_SUCCESS);

    return 1;
}

int TPipeWireDriver::stop()
{
    PENTER;
    m_running.store(0);

    if (m_threadLoop) {
        pw_thread_loop_lock(m_threadLoop);
        if (m_playbackStream) {
            pw_stream_set_active(m_playbackStream, false);
        }
        if (m_captureStream) {
            pw_stream_set_active(m_captureStream, false);
        }
        pw_thread_loop_unlock(m_threadLoop);
    }

    if (m_inputRingBuffer) {
        m_inputRingBuffer->reset();
    }
    if (m_outputRingBuffer) {
        m_outputRingBuffer->reset();
    }

    // silence capture channels
    TAudioDriver::stop();

    return 1;
}

void TPipeWireDriver::process_playback_cycle()
{
    nframes_t cycleFrames = m_framesPerCycle;

    if (m_enableCapture && m_inputRingBuffer) {
        uint captureChannelCount = m_captureChannels.size();
        size_t neededSamples = cycleFrames * captureChannelCount;
        if (m_interleavedInputBuffer.size() < neededSamples) {
            m_interleavedInputBuffer.resize(neededSamples);
        }

        size_t readSamples = m_inputRingBuffer->read(m_interleavedInputBuffer.data(), neededSamples);
        if (readSamples < neededSamples) {
            std::memset(m_interleavedInputBuffer.data() + readSamples, 0, (neededSamples - readSamples) * sizeof(audio_sample_t));
        }

        for (uint chan = 0; chan < captureChannelCount; ++chan) {
            audio_sample_t* dstChan = m_captureChannels.at(chan)->get_buffer().get_data(cycleFrames);
            for (nframes_t frame = 0; frame < cycleFrames; ++frame) {
                dstChan[frame] = m_interleavedInputBuffer[frame * captureChannelCount + chan];
            }
            m_captureChannels.at(chan)->process_monitoring();
        }
    }

    if (m_isSlave && m_ioPosition) {
        m_transportControl.set_location(TTimeRef(static_cast<nframes_t>(m_ioPosition->clock.position), audiodevice().get_sample_rate()));
        m_transportControl.set_realtime(true);
        m_transportControl.set_slave(true);
        m_device->transport_control(&m_transportControl);
    }

    m_runCycleStartTime = TTimeRef::get_nanoseconds_since_epoch();
    m_device->set_transport_cycle_start_time(m_runCycleStartTime);

    m_device->run_cycle(cycleFrames, 0.0);

    uint playbackChannelCount = m_playbackChannels.size();
    if (playbackChannelCount > 0 && m_outputRingBuffer) {
        size_t outputSamples = cycleFrames * playbackChannelCount;
        if (m_interleavedOutputBuffer.size() < outputSamples) {
            m_interleavedOutputBuffer.resize(outputSamples);
        }

        for (nframes_t frame = 0; frame < cycleFrames; ++frame) {
            for (uint chan = 0; chan < playbackChannelCount; ++chan) {
                m_interleavedOutputBuffer[frame * playbackChannelCount + chan] =
                    m_playbackChannels.at(chan)->get_buffer().at(frame);
            }
        }

        if (m_outputRingBuffer->write_space() < outputSamples) {
            m_outputRingBuffer->increment_read_ptr(outputSamples - m_outputRingBuffer->write_space());
        }
        m_outputRingBuffer->write(m_interleavedOutputBuffer.data(), outputSamples);
    }

    for (uint chan = 0; chan < playbackChannelCount; ++chan) {
        m_playbackChannels.at(chan)->silence_buffer();
    }

    m_runCycleEndTime = TTimeRef::get_nanoseconds_since_epoch();
    m_device->set_transport_cycle_end_time(m_runCycleEndTime);

    trav_time_t cycleDurationNs = m_runCycleEndTime - m_runCycleStartTime;
    uint32_t rate = audiodevice().get_sample_rate();
    if (rate > 0 && cycleFrames > 0) {
        trav_time_t periodTimeNs = static_cast<trav_time_t>((static_cast<double>(cycleFrames) / static_cast<double>(rate)) * 1e9);
        if (periodTimeNs > 0) {
            float load = (static_cast<float>(cycleDurationNs) / static_cast<float>(periodTimeNs)) * 100.0f;
            float prevLoad = m_cpuLoad.load();
            m_cpuLoad.store(prevLoad * 0.95f + load * 0.05f);
        }
    }
}

void TPipeWireDriver::process_capture_cycle()
{
    // NOTE: I think this function may be failing to get real audio data

    nframes_t cycleFrames = m_framesPerCycle;

    if (m_inputRingBuffer) {
        uint captureChannelCount = m_captureChannels.size();
        size_t neededSamples = cycleFrames * captureChannelCount;
        if (m_interleavedInputBuffer.size() < neededSamples) {
            m_interleavedInputBuffer.resize(neededSamples);
        }

        size_t readSamples = m_inputRingBuffer->read(m_interleavedInputBuffer.data(), neededSamples);
        if (readSamples < neededSamples) {
            std::memset(m_interleavedInputBuffer.data() + readSamples, 0, (neededSamples - readSamples) * sizeof(audio_sample_t));
        }

        for (uint chan = 0; chan < captureChannelCount; ++chan) {
            audio_sample_t* dstChan = m_captureChannels.at(chan)->get_buffer().get_data(cycleFrames);
            for (nframes_t frame = 0; frame < cycleFrames; ++frame) {
                dstChan[frame] = m_interleavedInputBuffer[frame * captureChannelCount + chan];
            }
            m_captureChannels.at(chan)->process_monitoring();
        }
    }

    if (m_isSlave && m_ioPosition) {
        m_transportControl.set_location(TTimeRef(static_cast<nframes_t>(m_ioPosition->clock.position), audiodevice().get_sample_rate()));
        m_transportControl.set_realtime(true);
        m_transportControl.set_slave(true);
        m_device->transport_control(&m_transportControl);
    }

    m_runCycleStartTime = TTimeRef::get_nanoseconds_since_epoch();
    m_device->set_transport_cycle_start_time(m_runCycleStartTime);

    m_device->run_cycle(cycleFrames, 0.0);

    m_runCycleEndTime = TTimeRef::get_nanoseconds_since_epoch();
    m_device->set_transport_cycle_end_time(m_runCycleEndTime);
}

void TPipeWireDriver::_on_playback_destroy(void *data)
{
    TPipeWireDriver* driver = static_cast<TPipeWireDriver*>(data);
    driver->m_playbackStream = nullptr;
}

void TPipeWireDriver::_on_playback_state_changed(void *data, enum pw_stream_state oldState, enum pw_stream_state state, const char *error)
{
    TPipeWireDriver* driver = static_cast<TPipeWireDriver*>(data);
    printf("PipeWire playback stream state: %s -> %s\n", pw_stream_state_as_string(oldState), pw_stream_state_as_string(state));
    if (state == PW_STREAM_STATE_ERROR) {
        printf("PipeWire playback stream error: %s\n", error ? error : "unknown");
        if (driver->m_running.exchange(2) != 2) {
            emit driver->pipewireShutDown();
        }
    } else if (state == PW_STREAM_STATE_UNCONNECTED) {
        if (driver->m_running.load() == 1) {
            printf("PipeWire playback stream disconnected\n");
            if (driver->m_running.exchange(2) != 2) {
                emit driver->pipewireShutDown();
            }
        }
    }
}

void TPipeWireDriver::_on_playback_process(void *data)
{
    TPipeWireDriver* driver = static_cast<TPipeWireDriver*>(data);
    if (!driver->m_playbackStream) {
        return;
    }

    struct pw_buffer* b = pw_stream_dequeue_buffer(driver->m_playbackStream);
    if (!b) {
        return;
    }

    struct spa_buffer* buf = b->buffer;
    float* dst = static_cast<float*>(buf->datas[0].data);
    if (!dst) {
        pw_stream_queue_buffer(driver->m_playbackStream, b);
        return;
    }

    uint channelCount = driver->m_playbackChannels.size();
    if (channelCount == 0) {
        pw_stream_queue_buffer(driver->m_playbackStream, b);
        return;
    }

    nframes_t nframes = driver->m_framesPerCycle;
    uint32_t stride = sizeof(float) * channelCount;

    size_t neededSamples = nframes * channelCount;

    if (!driver->is_running()) {
        std::memset(dst, 0, neededSamples * sizeof(float));
        buf->datas[0].chunk->offset = 0;
        buf->datas[0].chunk->stride = stride;
        buf->datas[0].chunk->size = neededSamples * sizeof(float);
        pw_stream_queue_buffer(driver->m_playbackStream, b);
        return;
    }

    if (driver->m_outputRingBuffer) {
        while (driver->m_outputRingBuffer->read_space() < neededSamples) {
            driver->process_playback_cycle();
        }
        size_t readSamples = driver->m_outputRingBuffer->read(dst, neededSamples);
        if (readSamples < neededSamples) {
            std::memset(dst + readSamples, 0, (neededSamples - readSamples) * sizeof(float));
        }
    } else {
        std::memset(dst, 0, neededSamples * sizeof(float));
    }

    buf->datas[0].chunk->offset = 0;
    buf->datas[0].chunk->stride = stride;
    buf->datas[0].chunk->size = neededSamples * sizeof(float);

    pw_stream_queue_buffer(driver->m_playbackStream, b);
}

void TPipeWireDriver::_on_capture_destroy(void *data)
{
    TPipeWireDriver* driver = static_cast<TPipeWireDriver*>(data);
    driver->m_captureStream = nullptr;
}

void TPipeWireDriver::_on_capture_state_changed(void *data, enum pw_stream_state oldState, enum pw_stream_state state, const char *error)
{
    TPipeWireDriver* driver = static_cast<TPipeWireDriver*>(data);
    printf("PipeWire capture stream state: %s -> %s\n", pw_stream_state_as_string(oldState), pw_stream_state_as_string(state));
    if (state == PW_STREAM_STATE_ERROR) {
        printf("PipeWire capture stream error: %s\n", error ? error : "unknown");
        if (driver->m_running.exchange(2) != 2) {
            emit driver->pipewireShutDown();
        }
    } else if (state == PW_STREAM_STATE_UNCONNECTED) {
        if (driver->m_running.load() == 1) {
            printf("PipeWire capture stream disconnected\n");
            if (driver->m_running.exchange(2) != 2) {
                emit driver->pipewireShutDown();
            }
        }
    }
}

void TPipeWireDriver::_on_capture_process(void *data)
{
    TPipeWireDriver* driver = static_cast<TPipeWireDriver*>(data);
    if (!driver->m_captureStream) {
        return;
    }

    struct pw_buffer* b = pw_stream_dequeue_buffer(driver->m_captureStream);
    if (!b) {
        return;
    }

    struct spa_buffer* buf = b->buffer;
    float* src = static_cast<float*>(buf->datas[0].data);
    uint channelCount = driver->m_captureChannels.size();

    if (src && channelCount > 0 && buf->datas[0].chunk) {
        uint32_t nframes = buf->datas[0].chunk->size / (sizeof(float) * channelCount);
        if (driver->m_inputRingBuffer) {
            size_t samples = nframes * channelCount;
            if (driver->m_inputRingBuffer->write_space() < samples) {
                driver->m_inputRingBuffer->increment_read_ptr(samples - driver->m_inputRingBuffer->write_space());
            }
            driver->m_inputRingBuffer->write(src, samples);

            if (!driver->m_enablePlayback && driver->is_running()) {
                size_t cycleSamples = driver->m_framesPerCycle * channelCount;
                while (driver->m_inputRingBuffer->read_space() >= cycleSamples) {
                    driver->process_capture_cycle();
                }
            }
        }
    }

    pw_stream_queue_buffer(driver->m_captureStream, b);
}

void TPipeWireDriver::_on_io_changed(void *data, uint32_t id, void *area, uint32_t size)
{
    Q_UNUSED(size);
    TPipeWireDriver* driver = static_cast<TPipeWireDriver*>(data);
    if (id == SPA_IO_Position) {
        driver->m_ioPosition = static_cast<struct spa_io_position*>(area);
    }
}

void TPipeWireDriver::_on_param_changed(void *data, uint32_t id, const struct spa_pod *param)
{
    TPipeWireDriver* driver = static_cast<TPipeWireDriver*>(data);
    if (!param || id != SPA_PARAM_Format) {
        return;
    }

    struct spa_audio_info_raw info = {};
    if (spa_format_audio_raw_parse(param, &info) < 0) {
        return;
    }

    if (info.rate > 0 && info.rate != driver->m_frameRate) {
        driver->m_frameRate = info.rate;
        driver->m_device->set_sample_rate(info.rate);
    }
}

void TPipeWireDriver::start_free_wheeling()
{
    m_isFreeWheeling = true;
    m_device->driver_changed_free_wheel_mode();
}

void TPipeWireDriver::stop_free_wheeling()
{
    m_isFreeWheeling = false;
    m_device->driver_changed_free_wheel_mode();
}

float TPipeWireDriver::get_cpu_load()
{
    return m_cpuLoad.load();
}

void TPipeWireDriver::update_config()
{
    m_isSlave = m_device->get_driver_property("pipewireslave", false).toBool();
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
