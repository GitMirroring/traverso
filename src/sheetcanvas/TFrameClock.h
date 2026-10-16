// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "TFrameRateFilter.h"

#include <QObject>
#include <QTimer>
#include <functional>
#include <vector>

class QWidget;

/**
 * @brief One clock for everything in the GUI that follows the audio
 *        transport: it ticks at the display's frame rate, only while a
 *        consumer needs it.
 *
 * A consumer registers once with the most updates per second it wants and
 * a tick function, and switches itself active while it has something to
 * follow (the playhead while the transport rolls). Every tick hands the
 * same time to all due consumers, so a playhead and a time label that read
 * the heard transport position with it agree (see
 * TSession::get_heard_transport_location()).
 *
 * The frame rate is the refresh rate of the window's screen when the clock
 * starts. In QML the ticks would come from FrameAnimation with the same
 * consumer interface.
 */
class TFrameClock : public QObject
{
    Q_OBJECT

public:
    using Tick = std::function<void(qint64 nowNanoseconds)>;

    explicit TFrameClock(QWidget* window);

    // The clock of the tick times, the same the audio thread stamps its
    // cycles with
    static qint64 now_nanoseconds();

    // consumer identifies the registration; the consumer removes it before
    // it is destroyed. tick runs in the GUI thread, at most maxHz times per
    // second, never within another consumer's tick
    void add_consumer(QObject* consumer, int maxHz, Tick tick);
    void remove_consumer(QObject* consumer);

    void set_consumer_active(QObject* consumer, bool active);
    bool is_consumer_active(QObject* consumer) const;

private:
    struct Consumer
    {
        QObject*         object;
        TFrameRateFilter filter;
        Tick             tick;
        bool             active{false};
    };

    QWidget*              m_window;
    QTimer                m_timer;
    std::vector<Consumer> m_consumers;
    qint64                m_frameIntervalNanoseconds{0};
    bool                  m_ticking{false};

    void start_or_stop();
    void tick();
};
