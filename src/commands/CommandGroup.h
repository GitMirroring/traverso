/*
    Copyright (C) 2005-2026 Remon Sijrier

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

    $Id: CommandGroup.h,v 1.4 2007/04/17 19:56:45 r_sijrier Exp $
*/

#pragma once

#include "TCommand.h"


class CommandGroup : public TCommand
{
public :
    CommandGroup(TContextItem* parent, const QString& des)
		: TCommand(parent, des)
	{
	}
	~CommandGroup();

    bool is_hold_command() const override  {return false;}
    int prepare_actions() override;
    int do_action() override;
    int undo_action() override;

	void add_command(TCommand* cmd) {
		Q_ASSERT(cmd);
        m_commands.push_back(cmd);
	}

private :
    std::vector<TCommand* >	m_commands;

};
