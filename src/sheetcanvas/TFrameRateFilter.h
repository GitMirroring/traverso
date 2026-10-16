// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QtGlobal>

/**
 * @brief Decides per frame clock tick whether a consumer that wants at most
 *        maxHz updates per second is due.
 *
 * Updates are due on a grid of the consumer's interval, and a tick within
 * half a frame before a grid point counts for it. A rate that does not
 * divide the frame rate then alternates between the two nearest frame
 * counts and keeps its average (8 Hz at 60 frames per second: 7 and 8
 * frames, 80 updates in ten seconds). A consumer that wants the frame rate
 * or more is due every tick. When the ticks stopped for a while the grid
 * starts over, so no updates are made up for.
 *
 * Times come from the caller, so the decision can be tested without a
 * clock.
 */
class TFrameRateFilter
{
public:
    explicit TFrameRateFilter(int maxHz)
        : m_intervalNanoseconds(maxHz > 0 ? 1000000000LL / maxHz : 0) {}

    // Marks the consumer updated when it is due
    bool due(qint64 nowNanoseconds, qint64 frameIntervalNanoseconds)
    {
        if (m_nextDue < 0) {
            m_nextDue = nowNanoseconds + m_intervalNanoseconds;
            return true;
        }

        if (nowNanoseconds < m_nextDue - frameIntervalNanoseconds / 2) {
            return false;
        }

        m_nextDue += m_intervalNanoseconds;
        if (m_nextDue < nowNanoseconds) {
            // Late by more than an interval: start the grid over
            m_nextDue = nowNanoseconds + m_intervalNanoseconds;
        }
        return true;
    }

    // The next tick is due, e.g. after the consumer was inactive
    void reset()
    {
        m_nextDue = -1;
    }

private:
    qint64 m_intervalNanoseconds;
    qint64 m_nextDue{-1};
};
