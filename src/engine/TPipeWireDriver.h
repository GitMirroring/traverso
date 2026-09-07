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

#ifndef TPIPEWIREDRIVER_H
#define TPIPEWIREDRIVER_H

#if defined (PIPEWIRE_SUPPORT)

#include "TAudioDriver.h"
#include "defines.h"
#include "RingBufferNPT.h"

#include <pipewire/pipewire.h>
#include <pipewire/stream.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/props.h>

#include <QObject>
#include <atomic>
#include <memory>
#include <vector>

class TPipeWireDriver : public TAudioDriver
{
    Q_OBJECT
public:
    explicit TPipeWireDriver(TAudioDevice* device);
    ~TPipeWireDriver() override;

    int _read(nframes_t nframes) override;
    int _write(nframes_t nframes) override;
    int _run_cycle() override { return 1; }
    int setup(bool capture = true, bool playback = true, const QString& cardDevice = "");
    int attach() override;
    int start() override;
    int stop() override;

    QString get_device_name() override;
    QString get_device_longname() override;

    float get_cpu_load();

    bool is_running() const { return m_running.load() == 1; }
    struct pw_stream* get_playback_stream() const { return m_playbackStream; }
    struct pw_stream* get_capture_stream() const { return m_captureStream; }
    bool is_slave() const { return m_isSlave; }
    void update_config();

    bool supports_software_channels() override {
        return false;
    }

    bool supports_free_wheeling() const override {
        return true;
    }

    void start_free_wheeling() final;
    void stop_free_wheeling() final;

    bool runs_in_blocking_mode() const override {
        return false;
    }

private:
    std::atomic<size_t>                 m_running{0};
    struct pw_thread_loop*              m_threadLoop{nullptr};
    struct pw_stream*                   m_playbackStream{nullptr};
    struct pw_stream*                   m_captureStream{nullptr};
    struct pw_stream_events             m_playbackEvents{};
    struct pw_stream_events             m_captureEvents{};

    struct spa_io_position*             m_ioPosition{nullptr};

    bool                                m_enableCapture{true};
    bool                                m_enablePlayback{true};
    QString                             m_cardDevice;

    std::unique_ptr<RingBufferNPT<audio_sample_t>> m_inputRingBuffer;
    std::vector<audio_sample_t>         m_interleavedInputBuffer;
    std::unique_ptr<RingBufferNPT<audio_sample_t>> m_outputRingBuffer;
    std::vector<audio_sample_t>         m_interleavedOutputBuffer;

    TTransportControl                   m_transportControl;
    bool                                m_isSlave{false};
    std::atomic<float>                  m_cpuLoad{0.0f};

    void process_playback_cycle();
    void process_capture_cycle();
    int fail_setup(const QString& message);

    static void _on_playback_destroy(void *data);
    static void _on_playback_state_changed(void *data, enum pw_stream_state oldState, enum pw_stream_state state, const char *error);
    static void _on_playback_process(void *data);

    static void _on_capture_destroy(void *data);
    static void _on_capture_state_changed(void *data, enum pw_stream_state oldState, enum pw_stream_state state, const char *error);
    static void _on_capture_process(void *data);

    static void _on_io_changed(void *data, uint32_t id, void *area, uint32_t size);
    static void _on_param_changed(void *data, uint32_t id, const struct spa_pod *param);

signals:
    void pipewireShutDown();
};

#endif // PIPEWIRE_SUPPORT

#endif // TPIPEWIREDRIVER_H
