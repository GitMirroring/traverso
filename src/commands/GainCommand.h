/*
    Copyright (C) 2010-2019 Remon Sijrier

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

#ifndef GAINCOMMAND_H
#define GAINCOMMAND_H

#include "TCommand.h"

class TContextItem;
class TAudioProcessingNode;

class GainCommand : public TCommand
{
    Q_OBJECT

public :
    GainCommand(TAudioProcessingNode *context, const QVariantList& args);
    ~GainCommand();

    int prepare_actions();
    int do_action();
    int undo_action();
    void cancel_action();

    int process_mouse_move(qreal diffY);
    void increase_gain();
    void decrease_gain();
    void set_new_gain(float newGain);
    void set_new_gain_numerical_input(float newGain);

private :
    TAudioProcessingNode*        m_audioProcessingNode;
    float 		m_origGain;
    float 		m_newGain;

    void apply_new_gain_to_object(float newGain);
};

#endif

