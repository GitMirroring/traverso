/*
    Copyright (C) 2010-2026 Remon Sijrier
    This file is part of Traverso
*/

#include "TMoveCommand.h"
#include "TClipsViewPort.h"
#include "TInputEventDispatcher.h"
#include "TProject.h"
#include "TProjectManager.h"
#include "TSheet.h"
#include "TSheetView.h"
#include "TContextPointer.h"
#include <QScrollBar>
#include "Debugger.h"
#include <cmath>
#include <QtGlobal> // Explicitly added to guarantee Q_ASSERT resolves cleanly

TMoveCommand::TMoveCommand(TSheetView *sv, TContextItem* item, const QString &description)
    : TCommand(item, description)
    , d(std::make_unique<Data>())
{
    d->sv = sv;
    d->speed = pm().get_project()->get_keyboard_arrow_key_navigation_speed();

    if (d->sv) {
        d->doSnap = d->sv->get_sheet()->is_snap_on();
    }

    connect(&d->shuttleTimer, &QTimer::timeout, this, &TMoveCommand::update_shuttle);
}

TMoveCommand::~TMoveCommand()
{
    PENTERDES;
    if (d) {
        stop_shuttle();
    }
}

void TMoveCommand::cancel_action()
{
    if (d) {
        stop_shuttle();
    }
    // EFFICIENT MEMORY RELEASE: Completely free the tracking context dynamically
    d.reset();
    undo_action();
}

int TMoveCommand::begin_hold()
{
    PENTER;
    Q_ASSERT(d != nullptr);

    bool dragShuttle = true;
    start_shuttle(dragShuttle);
    return 1;
}

int TMoveCommand::finish_hold()
{
    PENTER;
    if (d) {
        stop_shuttle();
    }
    // EFFICIENT MEMORY RELEASE: The active drag window is closed.
    // Release the unique_ptr context safely before storing this on the long-term history stack.
    d.reset();
    return 1;
}

int TMoveCommand::jog()
{
    Q_ASSERT(d != nullptr);
    if (!d->sv) {
        return -1;
    }

    auto direction = ShuttleDirection::RIGHT;
    qreal normalizedX = qreal(m_contextPointer->mouse_viewport_x()) / d->sv->get_clips_viewport()->width();
    qreal dragShuttleRange = 0.18;

    if (normalizedX < dragShuttleRange || normalizedX > (1.0 - dragShuttleRange)) {
        if (normalizedX < dragShuttleRange) {
            direction = ShuttleDirection::LEFT;
            normalizedX = -dragShuttleRange + normalizedX;
            normalizedX *= (1.0 / dragShuttleRange);
        }
        if (normalizedX > (1.0 - dragShuttleRange)) {
            direction = ShuttleDirection::RIGHT;
            normalizedX = normalizedX - (1.0 - dragShuttleRange);
            normalizedX *= (1.0 / dragShuttleRange);
        }
    } else {
        normalizedX = 0.0;
    }

    qreal value = d->shuttleCurve.valueForProgress(qAbs(normalizedX));
    if (std::abs(normalizedX) > 1.0) {
        value *= 1.5;
    }

    qreal viewportWidthScrollStep = d->sv->get_clips_viewport()->width() * dragShuttleRange * dragShuttleRange;
    d->shuttleXfactor = static_cast<int>(value * viewportWidthScrollStep * direction);

    dragShuttleRange = 0.1;
    direction = ShuttleDirection::UP;
    qreal normalizedY = qreal(m_contextPointer->mouse_viewport_y()) / d->sv->get_clips_viewport()->height();

    if (normalizedY < dragShuttleRange || normalizedY > (1.0 - dragShuttleRange)) {
        if (normalizedY < dragShuttleRange) {
            direction = ShuttleDirection::DOWN;
            normalizedY = -dragShuttleRange + normalizedY;
            normalizedY *= (1.0 / dragShuttleRange);
        }
        if (normalizedY > (1.0 - dragShuttleRange)) {
            direction = ShuttleDirection::UP;
            normalizedY = normalizedY - (1.0 - dragShuttleRange);
            normalizedY *= (1.0 / dragShuttleRange);
        }
    } else {
        normalizedY = 0.0;
    }

    value = d->shuttleCurve.valueForProgress(std::abs(normalizedY));
    qreal yscale = static_cast<qreal>(d->sv->get_mean_track_height()) * dragShuttleRange * 2.0;
    d->shuttleYfactor = static_cast<int>(value * yscale * direction);

    return 1;
}

void TMoveCommand::move_faster()
{
    Q_ASSERT(d != nullptr);
    if (d->speed > 32) {
        d->speed = 32;
    }

    if (d->speed == 1)       d->speed = 2;
    else if (d->speed == 2)  d->speed = 4;
    else if (d->speed == 4)  d->speed = 8;
    else if (d->speed == 8)  d->speed = 16;
    else if (d->speed == 16) d->speed = 32;

    pm().get_project()->set_keyboard_arrow_key_navigation_speed(d->speed);
    m_contextPointer->set_canvas_cursor_text(tr("Speed: %1").arg(d->speed), 1000);
}

void TMoveCommand::move_slower()
{
    Q_ASSERT(d != nullptr);
    if (d->speed > 32) {
        d->speed = 32;
    }

    if (d->speed == 32)      d->speed = 16;
    else if (d->speed == 16) d->speed = 8;
    else if (d->speed == 8)  d->speed = 4;
    else if (d->speed == 4)  d->speed = 2;
    else if (d->speed == 2)  d->speed = 1;

    pm().get_project()->set_keyboard_arrow_key_navigation_speed(d->speed);
    m_contextPointer->set_canvas_cursor_text(tr("Speed: %1").arg(d->speed), 1000);
}

void TMoveCommand::process_collected_number(const QString &collected)
{
    PENTER;
    Q_ASSERT(d != nullptr);

    int number = 0;
    bool ok = false;
    QString cleared = collected;
    cleared = cleared.remove(".").remove("-").remove(",");

    if (cleared.size() >= 1) {
        number = QString(cleared.data()[cleared.size() - 1]).toInt(&ok);
    }

    if (ok) {
        switch(number) {
        case 0:  d->speed = 1;   break;
        case 1:  d->speed = 2;   break;
        case 2:  d->speed = 4;   break;
        case 3:  d->speed = 8;   break;
        case 4:  d->speed = 16;  break;
        case 5:  d->speed = 32;  break;
        case 6:  d->speed = 64;  break;
        case 7:  d->speed = 128; break;
        case 8:  d->speed = 128; break;
        case 9:  d->speed = 128; break;
        default: d->speed = 2;
        }
        pm().get_project()->set_keyboard_arrow_key_navigation_speed(d->speed);
        m_contextPointer->set_canvas_cursor_text(tr("Speed: %1").arg(d->speed), 1000);
        ied().set_numerical_input("");
    }
}

void TMoveCommand::toggle_snap_on_off()
{
    Q_ASSERT(d != nullptr);
    TSheet* sheet = pm().get_project()->get_active_sheet();
    sheet->toggle_snap();
    d->doSnap = sheet->is_snap_on();

    if (d->doSnap) {
        m_contextPointer->set_canvas_cursor_text(tr("Snap On"), 1000);
    } else {
        m_contextPointer->set_canvas_cursor_text(tr("Snap Off"), 1000);
    }
}

void TMoveCommand::numerical_input()
{
    m_contextPointer->set_canvas_cursor_text(tr("Use numerical keys to set speed..."), 2000);
}

void TMoveCommand::start_shuttle(bool drag)
{
    Q_ASSERT(d != nullptr);
    if (!d->sv) {
        return;
    }

    d->shuttleCurve.setType(QEasingCurve::InOutQuad);
    d->shuttleTimer.start(40);
    d->dragShuttle = drag;
    d->shuttleYfactor = d->shuttleXfactor = 0;
    d->sv->stop_follow_play_head();
}

void TMoveCommand::stop_shuttle()
{
    Q_ASSERT(d != nullptr);
    if (d->shuttleTimer.isActive()) {
        d->shuttleTimer.stop();
    }
}

void TMoveCommand::update_shuttle()
{
    Q_ASSERT(d != nullptr);
    if (!d->sv) {
        return;
    }

    int x = d->sv->hscrollbar_value() + d->shuttleXfactor;
    d->sv->set_hscrollbar_value(x);

    int y = d->sv->vscrollbar_value() + d->shuttleYfactor;
    if (d->dragShuttle) {
        d->sv->set_vscrollbar_value(y);
    }

    if (d->shuttleXfactor != 0 || d->shuttleYfactor != 0) {
        ied().jog();
    }
}

void TMoveCommand::set_shuttle_factor_values(int x, int y)
{
    Q_ASSERT(d != nullptr);
    d->shuttleXfactor = x;
    d->shuttleYfactor = y;
}

void TMoveCommand::move_up()
{
    Q_ASSERT(d != nullptr);
    int step = d->sv->getVScrollBar()->pageStep();
    d->sv->set_vscrollbar_value(d->sv->vscrollbar_value() - (step * d->speed));
}

void TMoveCommand::move_down()
{
    Q_ASSERT(d != nullptr);
    int step = d->sv->getVScrollBar()->pageStep();
    d->sv->set_vscrollbar_value(d->sv->vscrollbar_value() + (step * d->speed));
}

void TMoveCommand::move_left()
{
    Q_ASSERT(d != nullptr);
    d->sv->set_hscrollbar_value(d->sv->hscrollbar_value() - (d->speed * 5));
}

void TMoveCommand::move_right()
{
    Q_ASSERT(d != nullptr);
    d->sv->set_hscrollbar_value(d->sv->hscrollbar_value() + (d->speed * 5));
}
