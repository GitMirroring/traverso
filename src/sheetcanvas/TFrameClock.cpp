// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TFrameClock.h"

#include "TRtTransportPosition.h"

#include <QScreen>
#include <QWidget>
#include <algorithm>

TFrameClock::TFrameClock(QWidget* window)
    : QObject(window)
    , m_window(window)
{
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &TFrameClock::tick);
}

qint64 TFrameClock::now_nanoseconds()
{
    return TRtTransportPosition::now_nanoseconds();
}

void TFrameClock::add_consumer(QObject* consumer, int maxHz, Tick tick)
{
    Q_ASSERT(!std::any_of(m_consumers.begin(), m_consumers.end(),
                          [consumer](const Consumer& c) { return c.object == consumer; }));

    m_consumers.push_back(Consumer{consumer, TFrameRateFilter(maxHz), std::move(tick)});
}

void TFrameClock::remove_consumer(QObject* consumer)
{
    Q_ASSERT_X(!m_ticking, "TFrameClock::remove_consumer", "a consumer cannot be removed during a tick");

    std::erase_if(m_consumers, [consumer](const Consumer& c) { return c.object == consumer; });
    start_or_stop();
}

void TFrameClock::set_consumer_active(QObject* consumer, bool active)
{
    for (Consumer& c : m_consumers) {
        if (c.object == consumer) {
            if (c.active != active) {
                c.active = active;
                c.filter.reset();
            }
            start_or_stop();
            return;
        }
    }

    Q_ASSERT_X(false, "TFrameClock::set_consumer_active", "not a registered consumer");
}

bool TFrameClock::is_consumer_active(QObject* consumer) const
{
    for (const Consumer& c : m_consumers) {
        if (c.object == consumer) {
            return c.active;
        }
    }
    return false;
}

// Runs while a consumer is active, at the refresh rate of the window's
// screen at the start
void TFrameClock::start_or_stop()
{
    const bool needed = std::any_of(m_consumers.begin(), m_consumers.end(),
                                    [](const Consumer& c) { return c.active; });

    if (needed == m_timer.isActive()) {
        return;
    }

    if (!needed) {
        m_timer.stop();
        return;
    }

    qreal refreshRate = 60.0;
    if (QScreen* screen = m_window->screen()) {
        if (screen->refreshRate() > 0) {
            refreshRate = screen->refreshRate();
        }
    }
    m_frameIntervalNanoseconds = qint64(1000000000.0 / refreshRate);
    m_timer.start(int(1000.0 / refreshRate));
}

void TFrameClock::tick()
{
    // A consumer that processes events can make the timer fire again
    // inside its own tick: one tick at a time
    if (m_ticking) {
        return;
    }
    m_ticking = true;

    const qint64 now = now_nanoseconds();

    // By index: a tick may activate or deactivate consumers
    for (size_t i = 0; i < m_consumers.size(); ++i) {
        Consumer& c = m_consumers[i];
        if (c.active && c.filter.due(now, m_frameIntervalNanoseconds)) {
            c.tick(now);
        }
    }

    m_ticking = false;
}
