// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "TTimeRef.h"

#include <QtGlobal>

#include <atomic>

class TIOScheduler;

/**
 * @brief What a TIOScheduler needs to know of one song (a sheet): where its
 * transport is, its seek, whether it plays, whether it records, and how many
 * idle steps the disk of its audio takes (doc/design/diskio-app-wide.md,
 * section 3; dispatcher-conclusie.md).
 *
 * The owner (the sheet) owns it and writes it, the audio thread its transport
 * and its seek; the scheduler thread reads it. All of it is atomics: nobody
 * waits. It is added to a scheduler before its streams (add_group()) and
 * removed after them (remove_group()).
 */
class TIOGroup
{
public:
    // The audio thread, every cycle the song rolls. While the song seeks the
    // scheduler writes it (the location the streams seek to); a song does not
    // roll while it seeks (TSheet), so the two never write at the same time.
    void set_transport_location(const TTimeRef& location)
    {
        Q_ASSERT_X(!m_seeking.load(std::memory_order_relaxed), "TIOGroup::set_transport_location",
                   "the transport moves while the song seeks");
        m_transportLocation.store(location.universal_frame(), std::memory_order_relaxed);
    }

    // One seek at a time: the owner asks for the next only after
    // seekFinished for this group (TSheet::initiate_seek_start(), also for a
    // JACK relocation). The location is written before the flag.
    void request_seek(const TTimeRef& location)
    {
        [[maybe_unused]] const bool seeking = m_seeking.exchange(true, std::memory_order_relaxed);
        Q_ASSERT_X(!seeking, "TIOGroup::request_seek", "a seek is asked for before the previous one finished");
        m_seekLocation.store(location.universal_frame(), std::memory_order_relaxed);
        m_seekRequested.store(true, std::memory_order_release);
    }

    // The song plays, or plays once its seek is done (a seek during
    // playback, play pressed during a seek): its seeks go before all
    // refills, and only its readers hold idle work back. The seek of a song
    // that does not play is idle work, a preparation to play.
    void set_playing(bool playing) { m_playing.store(playing, std::memory_order_relaxed); }

    // No idle work while any song records: the disk belongs to the recording
    void set_recording(bool recording) { m_recording.store(recording, std::memory_order_relaxed); }

    // At most this many idle steps at a time for the disk of the song's
    // audio (1 on a disk that seeks); the scheduler takes the lowest of its
    // songs
    void set_idle_limit(int limit) { m_idleLimit.store(limit, std::memory_order_relaxed); }
    int idle_limit() const { return m_idleLimit.load(std::memory_order_relaxed); }

private:
    friend class TIOScheduler;

    std::atomic<qint64> m_transportLocation{0};     // universal frames
    std::atomic<qint64> m_seekLocation{0};          // universal frames
    std::atomic<bool> m_seekRequested{false};
    // From the request until seekFinished, for the assert of one seek at a time
    std::atomic<bool> m_seeking{false};
    std::atomic<bool> m_playing{false};
    std::atomic<bool> m_recording{false};
    std::atomic<int> m_idleLimit{1};
};
