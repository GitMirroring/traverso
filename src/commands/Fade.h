/*
Copyright (C) 2006 Remon Sijrier 

This file is part of Traverso

Traverso is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA.

$Id: Fade.h,v 1.13 2008/01/21 16:22:11 r_sijrier Exp $
*/

#ifndef FADE_H
#define FADE_H

#include "TCommand.h"
#include "TMoveCommand.h"

#include <QPoint>

class TCurve;
class TAudioClip;
class TFadeCurve;
class TFadeCurveView;
class TSheetView;
class TSheet;

class FadeRange : public TMoveCommand
{
    Q_OBJECT

public :
    FadeRange(TAudioClip* clip, TFadeCurve* curve, qint64 scalefactor);
    FadeRange(TAudioClip* clip, TFadeCurve* curve, double newVal);
    ~FadeRange();

    int begin_hold();
    int finish_hold();
    int prepare_actions();
    int do_action();
    int undo_action();
    void cancel_action();

    int jog();

    void set_cursor_shape(int useX, int useY);
    bool restoreCursorPosition() const {return true;}

private :
    TFadeCurve*	m_curve;
    double 		m_origRange;
    double 		m_newRange;
    struct FadeRangePrivate {
        TSheet* sheet;
        TAudioClip* clip;
        int origX;
        int direction;
        qint64 scalefactor;
    };
    FadeRangePrivate* frp;

    void do_keyboard_move(double range);


public slots:
    void next_snap_pos();
    void prev_snap_pos();
    void move_left();
    void move_right();
    void reset_length();
};


class FadeStrength : public TCommand
{
    Q_OBJECT

public :
    FadeStrength(TFadeCurveView* fadeCurveView);
    FadeStrength(TFadeCurve* fade, double val);
    ~FadeStrength(){}

    int begin_hold();
    int finish_hold();
    int prepare_actions();
    int do_action();
    int undo_action();
    void cancel_action();

    int jog();

    void set_cursor_shape(int useX, int useY);
    bool restoreCursorPosition() const {return true;}

private :
    float	oldValue{};
    int	origY{};
    double	origStrength{};
    double	newStrength{};
    TFadeCurve*	m_fade;
    TFadeCurveView*	m_fv;
};


class FadeBend : public TCommand
{
    Q_OBJECT

public :
    FadeBend(TFadeCurveView* fadeCurveView);
    FadeBend(TFadeCurve* fade, double val);
    ~FadeBend(){}

    int begin_hold();
    int finish_hold();
    int prepare_actions();
    int do_action();
    int undo_action();
    void cancel_action();

    int jog();

    void set_cursor_shape(int useX, int useY);
    bool restoreCursorPosition() const {return true;}

private :
    float	oldValue{};
    int	origY{};
    double	origBend{};
    double	newBend{};
    TFadeCurve*	m_fade;
    TFadeCurveView*	m_fv;
};


class FadeMode : public TCommand
{
    Q_OBJECT

public :
    FadeMode(TFadeCurve* fade, int oldMode, int newMode);
    ~FadeMode(){}

    int prepare_actions();
    int do_action();
    int undo_action();
    bool is_hold_command() const {return false;}

private :
    int		m_oldMode;
    int		m_newMode;
    TFadeCurve*	m_fade;
};


#endif


