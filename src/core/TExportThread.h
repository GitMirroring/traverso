/*
Copyright (C) 2005-2006 Remon Sijrier 

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

$Id: Export.h,v 1.20 2009/05/07 19:59:03 n_doebelin Exp $
*/

#ifndef TEXPORTTHREAD_H
#define TEXPORTTHREAD_H

#include <QThread>
#include <QMap>



class Project;
class TExportSpecification;

class TExportThread : public QThread
{
	Q_OBJECT

public:
    TExportThread(Project* project);
    ~TExportThread()
	{}

	void run();

    void set_specification(TExportSpecification* spec);

private:
	Project*		m_project;
    TExportSpecification*	m_exportSpecification;
};


#endif
