// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

/**
 * @brief One piece of DiskIO's work: a stream's refill or seek, a writer's
 * flush, or a background task such as a peak build (doc/design/diskio-rounds.md).
 *
 * A round of a DiskIO thread asks every task whether it needs work and
 * runs one step of those that do, in parallel. A step is about one read of
 * a reader, so the steps of a round take about as long as each other.
 * needs_work() and is_done() are asked on the DiskIO thread between rounds,
 * never while a step runs; step() runs on a pool thread or on the DiskIO
 * thread itself, at most once per round.
 */
class TDiskIOTask
{
public:
    virtual ~TDiskIOTask() = default;
    // True when this task wants a step in the next round
    virtual bool needs_work() const = 0;
    // One unit of work, about one reader read
    virtual void step() = 0;
    // True when the task is over (background tasks only; streams never
    // finish): DiskIO deletes it after the round
    virtual bool is_done() const { return false; }
};
