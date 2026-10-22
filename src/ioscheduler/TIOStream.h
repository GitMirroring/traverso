// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "TIOPlanner.h"
#include "TTimeRef.h"
#include "defines.h"

/**
 * @brief What TIOScheduler asks of a stream it reads or writes: its state
 * for the planner, one step, and its ring buffers. The readers and writers in
 * core implement it; the reading, decoding, encoding and writing are theirs.
 */
class TIOStream
{
public:
    // The resampler of a stream that reads at another rate than its file's:
    // a TAudioResampler backend and one of its qualities, as the owner chose
    // them; the scheduler only hands them on
    struct Resampler
    {
        int backend{0};
        int quality{0};
        bool operator==(const Resampler&) const = default;
    };

    // What a step needs to know: the scheduler makes a copy for every step,
    // the step only reads it
    struct StepInfo
    {
        bool seek{false};
        TTimeRef seekLocation;
        TTimeRef transportLocation;
        bool sampleRateChanged{false};
        uint outputRate{0};
        Resampler resampler;
        nframes_t bufferSize{0};
    };

    virtual ~TIOStream() = default;

    // What the scheduler needs to know, the transport being at
    // transportLocation: whether it has work, whether its fill decides about
    // the idle work, its fill (one look at its buffers). On the scheduler
    // thread, never while a step of the stream runs.
    virtual TIOPlanner::Stream disk_state(const TTimeRef& transportLocation) = 0;
    // One step: a seek, back in sync, or read or write all there is room
    // for. The fill to show for it, -1 for nothing (out of sync). On a
    // worker thread, one step of a stream at a time.
    virtual int step(const StepInfo& info) = 0;
    // On the owner's thread before add_stream() returns, then on the
    // scheduler thread, never while a step of the stream runs
    virtual void set_output_rate_and_resampler(int outputRate, const Resampler& resampler) = 0;
    virtual uint get_channel_count() const = 0;
    // The ring buffers are made and freed only where the audio thread does
    // not use them: the scheduler knows when that is
    virtual void prepare_rt_buffers(nframes_t bufferSize) = 0;
    // The scheduler hands the stream back when it takes it out, on its
    // thread, when no step of it runs: the ring buffers go. Its owner deletes
    // it once remove_stream() returned.
    virtual void retire() = 0;
};
