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
    // What a step needs to know of the round: the scheduler writes it before
    // the round, the steps only read it
    struct StepInfo
    {
        bool seek{false};
        TTimeRef seekLocation;
        TTimeRef transportLocation;
        bool sampleRateChanged{false};
        uint outputRate{0};
        int resampleQuality{0};
        nframes_t bufferSize{0};
    };

    virtual ~TIOStream() = default;

    // What the next round needs to know, the transport being at
    // transportLocation: whether it has work, whether its fill decides about
    // the idle work, its fill (one look at its buffers)
    virtual TIOPlanner::Stream disk_state(const TTimeRef& transportLocation) = 0;
    // One step: a seek, back in sync, or read or write all there is room
    // for. The fill to show for it, -1 for nothing (out of sync).
    virtual int step(const StepInfo& info) = 0;
    virtual void set_output_rate_and_convertor_type(int outputRate, int converterType) = 0;
    virtual uint get_channel_count() const = 0;
    // The ring buffers are made and freed only where the audio thread does
    // not use them: the scheduler knows when that is
    virtual void prepare_rt_buffers(nframes_t bufferSize) = 0;
    // The scheduler hands the stream back right before it deletes it: the
    // ring buffers go
    virtual void retire() = 0;
};
