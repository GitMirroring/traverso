// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "TTimeRef.h"

#include <algorithm>
#include <cmath>

/**
 * @brief How far a recorded take is shifted earlier
 * (doc/design/recording-latency.md).
 *
 * A performer plays to what they hear: the output of a cycle is heard the
 * playback latency later, and what they play reaches a cycle's input the
 * capture latency later still. So a frame captured while the transport is
 * at Q belongs to the timeline at Q minus both. A take stays where the
 * recording started and its writer drops the first frames, as many as the
 * compensation: they came in before anything could be heard. The file then
 * starts with the frame that belongs to the take's position.
 */
struct TRecordingLatency
{
    // In frames of rate. extraMilliseconds is the user's part for what the
    // driver does not know (converters, external gear), and may be negative;
    // the result is never below 0. A software input (the bounce track, an
    // internal bus) is aligned in its own cycle: 0.
    static qint64 compensation_frames(qint64 captureFrames, qint64 playbackFrames, double extraMilliseconds, uint rate,
                                      bool hardwareInput, bool enabled)
    {
        if (!enabled || !hardwareInput || rate == 0) {
            return 0;
        }
        const qint64 extra = std::llround(TTimeRef::to_frames(TTimeRef::from_milliseconds(extraMilliseconds), rate));
        return std::max<qint64>(0, captureFrames + playbackFrames + extra);
    }

    // A take's length on the timeline once frames were captured: what lies
    // past the compensation
    static qint64 visible_frames(qint64 captured, qint64 compensation)
    {
        return std::max<qint64>(0, captured - compensation);
    }

    // The frames of a buffer of frames the writer drops, while remaining are
    // still to be dropped; remaining goes down by them
    static qint64 frames_to_drop(qint64& remaining, qint64 frames)
    {
        const qint64 drop = std::clamp<qint64>(remaining, 0, frames);
        remaining -= drop;
        return drop;
    }
};
