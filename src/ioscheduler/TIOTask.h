// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

/**
 * @brief Background work of a DiskIO thread, such as a peak build
 * (doc/design/diskio-rounds.md).
 *
 * A round runs one step of as many of these as the planner lets in, next to
 * the streams' steps. A step is about one read of a reader, so the steps of
 * a round take about as long as each other. step() runs on a pool thread or
 * on the DiskIO thread itself, at most once per round.
 */
class TIOTask
{
public:
    virtual ~TIOTask() = default;
    // One unit of work, about one reader read; false when the task is over
    // (DiskIO hands it back and deletes it after the round)
    virtual bool step() = 0;
    // DiskIO hands the task back right before it deletes it: when it is
    // over (on the DiskIO thread, after the round), or when DiskIO goes
    // first (on the thread that deletes DiskIO). Reporting a result belongs
    // here, not in the destructor.
    virtual void retire() {}
};
