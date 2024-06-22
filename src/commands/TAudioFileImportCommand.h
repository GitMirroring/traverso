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
 
    $Id: Import.h,v 1.10 2008/05/25 13:59:10 n_doebelin Exp $
*/

#ifndef IMPORT_H
#define IMPORT_H

#include "TCommand.h"

#include "TTimeRef.h"

class QString;
class AudioClip;
class AudioTrack;
class ReadSource;

class TAudioFileImportCommand : public TCommand
{
    Q_OBJECT

public :
    TAudioFileImportCommand(ContextItem* context);
    ~TAudioFileImportCommand();

    int prepare_actions();
    int do_action();
    int undo_action();
    bool is_hold_command() const {return false;}

    int create_readsource();
    void create_audioclip();
    void set_file_name(const QString& fileName);
    void set_track(AudioTrack* track);
    void set_import_location(const TTimeRef& location);
    void set_length(const TTimeRef& length);
    void set_silent(bool silent);
    ReadSource* readsource() {return m_readSource;}

private :
    AudioTrack*     m_track;
    AudioClip*      m_clip;
    ReadSource* 	m_readSource;
    QString         m_fileName;
    QString         m_name;
    TTimeRef		m_initialLength;
    TTimeRef		m_importLocation;
    bool            m_silent;
    bool            m_hasPosition;

    void init(AudioTrack* track, const QString& filename);
};

#endif

