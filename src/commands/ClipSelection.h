/*
Copyright (C) 2005-2008 Remon Sijrier 

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

#ifndef CLIPSELECTION_H
#define CLIPSELECTION_H

#include "TCommand.h"
#include <QList>
#include <QRect>
#include <QVariantList>

class TAudioClip;
class TAudioClipManager;

class ClipSelection : public TCommand
{
        Q_OBJECT

public :
    ClipSelection(TAudioClip* clip, QVariantList args);
	ClipSelection(QList<TAudioClip*> clips, TAudioClipManager* manager, const char* slot, const QString& des);
	~ClipSelection();

    int prepare_actions() {return 1;}
    int do_action();
    bool is_hold_command() const {return false;}

private:
    using TAudioClipManagerMethod = void (TAudioClipManager::*)(TAudioClip*);

    TAudioClipManagerMethod m_methodPointer{nullptr};
    QString m_slotName; // Keep this ONLY for the PERROR debug print if needed

	QList<TAudioClip* >	m_clips;
	TAudioClipManager* 	m_acmanager;
};

#endif



