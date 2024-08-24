/*
    Copyright (C) 2005-2007 Remon Sijrier 
 
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

#include "TInformUser.h"
#include "Utils.h"
#include "TAudioDevice.h"

#include "Debugger.h"


TInformUser& tInformUser()
{
        static TInformUser information;
        return information;
}


TInformUser::TInformUser()
{
    connect(&audiodevice(), &TAudioDevice::newDriverSetupMessage, this, &TInformUser::audiodevice_message);
}


void TInformUser::information( const QString & mes )
{
        TInformUserData s;
        s.message = mes;
        s.type = INFO;
	PMESG("Information::information %s", QS_C(mes));
	emit message(s);
}

void TInformUser::warning( const QString & mes )
{
        TInformUserData s;
        s.message = mes;
        s.type = WARNING;
        PWARN(QString("Information::warning %1").arg(mes).toLatin1().data());
        emit message(s);
}


void TInformUser::critical( const QString & mes )
{
        TInformUserData s;
        s.message = mes;
        s.type = CRITICAL;
//	PERROR("Information::critical %s", QS_C(mes));
	emit message(s);
}

void TInformUser::audiodevice_message()
{
    auto messages = audiodevice().get_audio_driver_setup_messages();
    for (const auto &message : messages) {
        switch(message.severity) {
            case TAudioDevice::INFO: information(message.message);
            break;
            case TAudioDevice::WARNING: warning(message.message);
            break;
            case TAudioDevice::CRITICAL: critical(message.message);
            break;
            default: ;// do nothing;
        }
    }
}
