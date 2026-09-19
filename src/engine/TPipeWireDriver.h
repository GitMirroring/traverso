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

#pragma once

#if defined (PIPEWIRE_SUPPORT)

#include "TAudioDriver.h"

#include <pipewire/pipewire.h>
#include <pipewire/stream.h>
#include <pipewire/loop.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/audio/layout.h>

#include <QSocketNotifier>


class TPipeWireDriver : public TAudioDriver
{
    Q_OBJECT
public:
    explicit TPipeWireDriver(TAudioDevice* device);
    ~TPipeWireDriver() override;

    int  process_callback ();
    int process_capture_callback();

    int setup(bool capture = true, bool playback = true, const QString& cardDevice = "");
    int attach() override;
    int start() override;
    int stop() override;

    QString get_device_name() override;
    QString get_device_longname() override;

    bool supports_software_channels() override {
        return false;
    }

    bool supports_free_wheeling() const override {
        return false;
    }

    bool runs_in_blocking_mode() const override {
        return false;
    }

protected:
    int _run_cycle() override;
    int _read(nframes_t nframes) override;
    int _write(nframes_t nframes)  override;

private:

    struct pw_loop *m_pwLoop{nullptr};
    struct pw_stream *m_playbackStream{nullptr};
    struct pw_stream_events m_playbackStreamEvents;
    struct pw_stream *m_captureStream{nullptr};
    struct pw_stream_events m_captureStreamEvents;

    QSocketNotifier *m_notifier{nullptr};

    int setup_failed(const QString& message);

    // callback functions for pipewire
    static void _on_process_playback(void *userdata);
    static void _on_state_changed(void *userdata, enum pw_stream_state old_state, enum pw_stream_state state, const char *error);
    static void _on_process_capture(void *userdata);
//    static void _on_param_changed(void *userdata, void *port_data, uint32_t id, const struct spa_pod *param);

    void handle_state_changed(pw_stream_state old_state, pw_stream_state state, const char *error);


private slots:
    void handle_pipewire_events();

signals:
    void pipewireShutDown();
};

#endif // PIPEWIRE_SUPPORT

