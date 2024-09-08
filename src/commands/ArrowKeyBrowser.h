/*
Copyright (C) 2010 Remon Sijrier

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

*/

#ifndef ARROWKEYBROWSER_H
#define ARROWKEYBROWSER_H

#include "TCommand.h"
#include <QTimer>
#include <QVariantList>

class TSheetView;

class ArrowKeyBrowser : public TCommand
{
    Q_OBJECT
public:
    explicit ArrowKeyBrowser(TSheetView* sv);

    int prepare_actions() {return 1;}
    int do_action();
    int undo_action() {return 1;};
    int begin_hold() {return 1;};
    int finish_hold() {return -1;};

    void set_cursor_shape(int useX, int useY);
    bool supportsEnterFinishesHold() const {return false;}

private:
    TSheetView*     m_sv;
    int             m_action;

    enum {
        LEFT = 0,
        RIGHT,
        UP,
        DOWN
    };

public slots:
    void up();
    void down();
    void left();
    void right();
};

#endif // ARROWKEYBROWSER_H
