// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QtGlobal>

/**
 * @brief Decides from the audio thread's cycle count whether it stopped
 *        processing.
 *
 * The audio thread counts its process cycles; the GUI thread checks the count
 * regularly. The audio thread is stalled when the count did not change for
 * stallMilliseconds while it should run: a thread that hangs, or one that
 * still runs but no longer completes its cycles (e.g. after memory
 * corruption). A busy GUI or many posted changes do not count as a stall, the
 * audio thread keeps counting through those.
 *
 * Times come from the caller, so the decision can be tested without threads
 * or clocks.
 */
class TAudioThreadWatchdog
{
public:
    explicit TAudioThreadWatchdog(qint64 stallMilliseconds = 2000)
        : m_stallMilliseconds(stallMilliseconds) {}

    // The audio thread (re)started: it gets stallMilliseconds for its first cycle
    void restart(quint64 cycleCount, qint64 nowMilliseconds)
    {
        m_lastCycleCount = cycleCount;
        m_lastProgress = nowMilliseconds;
    }

    // True when no cycle completed during the last stallMilliseconds
    bool is_stalled(quint64 cycleCount, qint64 nowMilliseconds)
    {
        if (cycleCount != m_lastCycleCount) {
            restart(cycleCount, nowMilliseconds);
            return false;
        }
        return nowMilliseconds - m_lastProgress >= m_stallMilliseconds;
    }

private:
    qint64  m_stallMilliseconds;
    quint64 m_lastCycleCount{0};
    qint64  m_lastProgress{0};
};
