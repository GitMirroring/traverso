/*
Copyright (C) 2011 Remon Sijrier

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

#include "TGlobalContext.h"

#include "TProjectManager.h"
#include "TProject.h"
#include "TSheet.h"

TGlobalContext::TGlobalContext(QObject *parent) :
    QObject(parent)
{
    m_session = nullptr;
    m_project = nullptr;

    connect(&pm(), &TProjectManager::projectLoaded, this, &TGlobalContext::set_project);
}

void TGlobalContext::set_project(TProject *project)
{
    PENTER;

    if (m_project) {
        disconnect(m_project, &TProject::currentSessionChanged, this, &TGlobalContext::set_session);
    }

	m_project = project;

    if (m_project) {
        connect(m_project, &TProject::currentSessionChanged, this, &TGlobalContext::set_session);
	} else {
        set_session(nullptr);
	}
}

void TGlobalContext::set_session(TSession *session)
{
    PENTER;
	m_session = session;
}
