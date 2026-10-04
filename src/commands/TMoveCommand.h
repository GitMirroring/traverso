/*
    Copyright (C) 2010-2026 Remon Sijrier
    This file is part of Traverso
*/

#pragma once

#include "TCommand.h"
#include <QEasingCurve>
#include <QTimer>
#include <memory> // Added for std::unique_ptr memory management

class TSheetView;

/**
 * @class TMoveCommand
 * @brief Base class for handling interactive jog, shuttle, and structural coordinate translations across the timeline.
 */
class TMoveCommand : public TCommand
{
    Q_OBJECT

public:
    TMoveCommand(TSheetView* sv, TContextItem* item, const QString& description);
    virtual ~TMoveCommand() override;

    // Fixed virtual signatures with explicit compiler override protections
    int begin_hold() override;
    int finish_hold() override;
    void cancel_action() override;
    int jog() override;
    void process_collected_number(const QString& collected) override;

protected:
    void start_shuttle(bool drag = false);
    void stop_shuttle();
    void update_shuttle_factor();
    void set_shuttle_factor_values(int x, int y);

    struct Data {
        TSheetView*  sv{nullptr};
        QTimer       shuttleTimer;
        QEasingCurve shuttleCurve;
        bool         dragShuttle{false};
        int          shuttleXfactor{0};
        int          shuttleYfactor{0};
        int          speed{1};
        bool         doSnap{false};
    };

    std::unique_ptr<Data> d;

private:
    enum ShuttleDirection {
        LEFT  = -1,
        RIGHT =  1,
        UP    =  1,
        DOWN  = -1
    };

public slots:
    void move_faster();
    void move_slower();
    void toggle_snap_on_off();
    void numerical_input();

    virtual void move_up();
    virtual void move_down();
    virtual void move_left();
    virtual void move_right();

private slots:
    void update_shuttle();
};

