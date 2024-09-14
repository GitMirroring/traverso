/*
    Copyright (C) 2024 Remon Sijrier

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

#ifndef TAUDIOCLIPDUALTRIMCOMMAND_H
#define TAUDIOCLIPDUALTRIMCOMMAND_H

#include "TMoveCommand.h"
#include "TTimeRef.h"

class TSheetView;
class TContextItem;
class TAudioClip;
class TAudioTrack;

class TAudioClipDualTrimCommand : public TMoveCommand
{
public:
    TAudioClipDualTrimCommand(TSheetView* sv, TAudioTrack* audioTrack);
    ~TAudioClipDualTrimCommand() {}

    int prepare_actions();
    int do_action();
    int undo_action();

    int begin_hold();
    int finish_hold();
    void cancel_action();

    int jog();

private:

    void set_canvas_cursor_text(const QString & text);

    TAudioTrack*    m_audioTrack{nullptr};
    TAudioClip*     m_leftAudioClip{nullptr};
    TAudioClip*     m_rightAudioClip{nullptr};
    TTimeRef        m_origLocation{};
    TTimeRef        m_newLocation{};

};

#endif // TAUDIOCLIPDUALTRIMCOMMAND_H
