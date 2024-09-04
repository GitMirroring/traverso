/*
Copyright (C) 2007-2010 Remon Sijrier

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

#ifndef TRAVERSO_COMMANDS_H
#define TRAVERSO_COMMANDS_H


#include <TCommandPlugin.h>

class TShortCutFunction;
class TShortCutManager;

const bool USE_X = true;
const bool USE_Y = true;
const bool NO_X = false;
const bool NO_Y = false;

class TraversoCommands : public TCommandPlugin
{
	Q_OBJECT

public:
	TraversoCommands();

    void load(TShortCutManager* m);
    TCommand* create(QObject* obj, const QString& commandName, QVariantList arguments);

private:
    enum TraversoCommand {
        NoCommand,
		GainCommand,
		TrackPanCommand,
		ImportAudioCommand,
		InsertSilenceCommand,
		AddNewAudioTrackCommand,
		RemoveClipCommand,
		RemoveTrackCommand,
        RemoveCurveNodeCommmand,
		RemovePluginCommand,
		AudioClipExternalProcessingCommand,
		ClipSelectionCommand,
		MoveClipCommand,
		MoveTrackCommand,
		MoveClipOrEdgeCommand,
		SplitClipCommand,
		CropClipCommand,
		ArmTracksCommand,
		ZoomCommand,
		ScrollCommand,
		ShuttleCommand,
		NormalizeClipCommand,
		ArrowKeyBrowserCommand,
		WorkCursorMoveCommand,
		MoveEdgeCommand,
		MoveCurveNodesCommand,
        MoveMarkerCommand,
        MovePluginCommand,
        FadeRangeCommand,
        FadeCurveBendCommand,
        FadeCurveStrengthCommand,
        GainShowAutomationCommand,
        TransportSetPositionCommand
	};

private:
    void add_function(const QMetaObject *metaObject,
                      const QString &description,
                      const char *commandName,
                      TraversoCommand command=NoCommand,
                      const char *slotSignature = "",
                      bool useX = false,
                      bool useY = false,
                      const QVariantList &args = QVariantList())
    {
        add_function(metaObject, nullptr, description, commandName, command, slotSignature, useX, useY, args);
    }

    void add_function(const QMetaObject *metaObject,
                      const QMetaObject *inheritedMetaObject,
                      const QString &description,
                      const char *commandName,
                      TraversoCommand command=NoCommand,
                      const char *slotSignature = "",
                      bool useX = false,
                      bool useY = false,
                      const QVariantList &args = QVariantList());

};

class TResetBase : public QObject
{
    Q_OBJECT
};
class TToggleBypassBase : QObject
{
    Q_OBJECT
};
class TDeleteBase : QObject
{
    Q_OBJECT
};
class TMoveBase : QObject
{
    Q_OBJECT
};
class TGainBase : QObject
{
    Q_OBJECT
};
class TToggleVerticalBase : QObject
{
    Q_OBJECT
};
class TEditPropertiesBase : QObject
{
    Q_OBJECT
};

#endif

//eof
