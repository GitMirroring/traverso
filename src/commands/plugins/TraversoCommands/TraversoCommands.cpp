/*
Copyright (C) 2007-2024 Remon Sijrier

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

#include "TraversoCommands.h"

#include <QInputDialog>
#include <QStringList>

#include "TAudioClipManager.h"
#include "TAudioClip.h"
#include "TAudioTrack.h"
#include "TCurve.h"
#include "TFadeCurve.h"
#include "TAudioPlugin.h"
#include "TInformUser.h"
#include "TProject.h"
#include "TProjectManager.h"
#include "TSheet.h"
#include "TBusTrack.h"
#include "TInputEventDispatcher.h"
#include "TShortCutFunction.h"
#include "TTimeLineRuler.h"
#include "libtraversosheetcanvas.h"
#include "commands.h"
#include <cfloat>
#include "TMainWindow.h"
#include "TTransport.h"
#include "TShortCutManager.h"
#include "widgets/SpectralMeterView.h"
#include "widgets/CorrelationMeterView.h"

#include "Debugger.h"

/**
 *	\class TraversoCommands
    \brief The Traverso TCommandPlugin class which 'implements' many of the default Commands

    With this plugin, the TInputEventDispatcher is able to dispatch key actions by directly
    asking this TAudioPlugin for the needed Command object.
 */


TraversoCommands::TraversoCommands()
{
}

void TraversoCommands::load()
{
    tShortCutManager().add_meta_object(&TSheet::staticMetaObject);
    tShortCutManager().add_meta_object(&TSheetView::staticMetaObject);
    tShortCutManager().add_meta_object(&TAudioTrack::staticMetaObject);
    tShortCutManager().add_meta_object(&TAudioTrackView::staticMetaObject);
    tShortCutManager().add_meta_object(&TBusTrack::staticMetaObject);
    tShortCutManager().add_meta_object(&TBusTrackView::staticMetaObject);
    tShortCutManager().add_meta_object(&TAudioClip::staticMetaObject);
    tShortCutManager().add_meta_object(&TAudioClipView::staticMetaObject);
    tShortCutManager().add_meta_object(&TCurve::staticMetaObject);
    tShortCutManager().add_meta_object(&CurveView::staticMetaObject);
    tShortCutManager().add_meta_object(&TTimeLineRuler::staticMetaObject);
    tShortCutManager().add_meta_object(&TimeLineView::staticMetaObject);
    tShortCutManager().add_meta_object(&TAudioPlugin::staticMetaObject);
    tShortCutManager().add_meta_object(&TAudioPluginView::staticMetaObject);
    tShortCutManager().add_meta_object(&TFadeCurve::staticMetaObject);
    tShortCutManager().add_meta_object(&TFadeCurveView::staticMetaObject);
    tShortCutManager().add_meta_object(&TMainWindow::staticMetaObject);
    tShortCutManager().add_meta_object(&TProjectManager::staticMetaObject);
    tShortCutManager().add_meta_object(&TGainGroupCommand::staticMetaObject);
    tShortCutManager().add_meta_object(&MoveTrack::staticMetaObject);
    tShortCutManager().add_meta_object(&MoveClip::staticMetaObject);
    tShortCutManager().add_meta_object(&MovePlugin::staticMetaObject);
    tShortCutManager().add_meta_object(&MoveCurveNode::staticMetaObject);
    tShortCutManager().add_meta_object(&Zoom::staticMetaObject);
    tShortCutManager().add_meta_object(&TrackPan::staticMetaObject);
    tShortCutManager().add_meta_object(&MoveMarker::staticMetaObject);
    tShortCutManager().add_meta_object(&WorkCursorMove::staticMetaObject);
    tShortCutManager().add_meta_object(&PlayHeadMove::staticMetaObject);
    tShortCutManager().add_meta_object(&MoveEdge::staticMetaObject);
    tShortCutManager().add_meta_object(&CropClip::staticMetaObject);
    tShortCutManager().add_meta_object(&FadeRange::staticMetaObject);
    tShortCutManager().add_meta_object(&SplitClip::staticMetaObject);
    tShortCutManager().add_meta_object(&TPanKnobView::staticMetaObject);
    tShortCutManager().add_meta_object(&TMoveCommand::staticMetaObject);
    tShortCutManager().add_meta_object(&TTransport::staticMetaObject);
    tShortCutManager().add_meta_object(&SpectralMeterView::staticMetaObject);
    tShortCutManager().add_meta_object(&CorrelationMeterView::staticMetaObject);


    tShortCutManager().add_translation("TTransport", tr("Transport"));

    tShortCutManager().add_translation("ArrowKeyBrowser", tr("Arrow Key Browser"));
    tShortCutManager().add_translation("ArmTracks", tr("Arm Tracks"));
    tShortCutManager().add_translation("CropClip", tr("Cut Clip (Magnetic)"));
    tShortCutManager().add_translation("Fade", tr("Fade In/Out"));
    tShortCutManager().add_translation("TGainGroupCommand", tr("Gain"));
    tShortCutManager().add_translation("MoveClip", tr("Move Clip"));
    tShortCutManager().add_translation("MoveCurveNode", tr("Move Node"));
    tShortCutManager().add_translation("MoveEdge", tr("Move Clip Edge"));
    tShortCutManager().add_translation("MoveMarker", tr("Move Marker"));
    tShortCutManager().add_translation("MoveTrack", tr("Move Track"));
    tShortCutManager().add_translation("MovePlugin", tr("Move Plugin"));
    tShortCutManager().add_translation("PlayHeadMove", tr("Move Play Head"));
    tShortCutManager().add_translation("Shuttle", tr("Shuttle"));
    tShortCutManager().add_translation("SplitClip", tr("Split Clip"));
    tShortCutManager().add_translation("TrackPan", tr("Track Pan"));
    tShortCutManager().add_translation("TPanKnob", tr("Pan Knob"));
    tShortCutManager().add_translation("TPanKnobView", tr("Pan Knob"));

    tShortCutManager().add_translation("WorkCursorMove", tr("Move Work Cursor"));
    tShortCutManager().add_translation("Zoom", tr("Zoom"));

    tShortCutManager().add_translation("TAudioClip",tr("Audio Clip"));
    tShortCutManager().add_translation("TAudioTrack", tr("Audio Track"));
    tShortCutManager().add_translation("TCurve",tr("Curve"));
    tShortCutManager().add_translation("TCurveNode",tr("Curve Node"));
    tShortCutManager().add_translation("TFadeCurve",tr("Fade Curve"));
    tShortCutManager().add_translation("FadeRange", tr("Fade Length"));
    tShortCutManager().add_translation("FadeBend", tr("Bend Factor"));
    tShortCutManager().add_translation("FadeStrength", tr("Strength Factor"));
    tShortCutManager().add_translation("TTimeLineMarker",tr("Marker"));
    tShortCutManager().add_translation("TSheet",tr("Sheet"));
    tShortCutManager().add_translation("TBusTrack",tr("Bus Track"));
    tShortCutManager().add_translation("TTimeLineRuler",tr("Time Line"));
    tShortCutManager().add_translation("TBusTrackPanel", tr("Bus Track"));
    tShortCutManager().add_translation("TMainWindow", tr("Global"));
    tShortCutManager().add_translation("TProjectManager", tr("Project Manager"));
    tShortCutManager().add_translation("TrackPanelGain", tr("Gain"));
    tShortCutManager().add_translation("TrackPanelPan", tr("Panorama"));
    tShortCutManager().add_translation("TrackPanelLed", tr("Track Panel Button"));
    tShortCutManager().add_translation("TrackPanelBus", tr("Routing Indicator"));
    tShortCutManager().add_translation("VUMeterLevel", tr("VU Level"));
    tShortCutManager().add_translation("VUMeter", tr("VU Level"));
    tShortCutManager().add_translation("AudioTrackPanel",tr("Audio Track"));
    tShortCutManager().add_translation("TAudioPlugin",tr("Plugin"));
    tShortCutManager().add_translation("PlayHead", tr("Play Head"));
    tShortCutManager().add_translation("PositionIndicator", tr("Position Indicator"));
    tShortCutManager().add_translation("WorkCursor", tr("Work Cursor"));
    tShortCutManager().add_translation("Playhead", tr("Play Cursor"));
    tShortCutManager().add_translation("CorrelationMeter", tr("Correlation Meter"));
    tShortCutManager().add_translation("SpectralMeter", tr("Spectral Analyzer"));
    tShortCutManager().add_translation("TTrack", tr("Track"));
    tShortCutManager().add_translation("TMoveCommand", tr("Shuttle"));
    tShortCutManager().add_translation("EditProperties", tr("Edit Properties"));
    tShortCutManager().add_translation("TAudioProcessingNode", tr("Audio Processing Node"));
    tShortCutManager().add_translation("ToggleBypass", tr("Toggle Bypass"));
    tShortCutManager().add_translation("HoldCommand", tr("Hold Command"));
    tShortCutManager().add_translation("Navigate", tr("Navigate"));

    TShortCutFunction* function;

    function = new TShortCutFunction();
    function->set_metaobject(&ArrowKeyBrowser::staticMetaObject);
    function->set_slot_signature("up");
    function->set_description(tr("Up"));
    function->commandName = "ArrowKeyBrowserUp";
    add_function(function, ArrowKeyBrowserCommand);

    function = new TShortCutFunction();
    function->set_metaobject(&ArrowKeyBrowser::staticMetaObject);
    function->set_slot_signature("down");
    function->set_description(tr("Down"));
    function->commandName = "ArrowKeyBrowserDown";
    add_function(function, ArrowKeyBrowserCommand);

    function = new TShortCutFunction();
    function->set_metaobject(&ArrowKeyBrowser::staticMetaObject);
    function->set_slot_signature("left");
    function->set_description(tr("Left"));
    function->commandName = "ArrowKeyBrowserLeft";
    add_function(function, ArrowKeyBrowserCommand);

    function = new TShortCutFunction();
    function->set_metaobject(&ArrowKeyBrowser::staticMetaObject);
    function->set_slot_signature("right");
    function->set_description(tr("Right"));
    function->commandName = "ArrowKeyBrowserRight";
    add_function(function, ArrowKeyBrowserCommand);

    function = new TShortCutFunction();
    function->set_metaobject(&TAudioTrack::staticMetaObject);
    function->set_description(tr("Import Audio"));
    function->commandName = "ImportAudio";
    add_function(function, ImportAudioCommand);

    function = new TShortCutFunction();
    function->set_metaobject(&TAudioTrackView::staticMetaObject);
    function->set_description(tr("Fold Track"));
    function->commandName = "FoldTrack";
    function->useX = true;
    function->arguments << "fold_track";
    add_function(function, MoveClipCommand);

    function = new TShortCutFunction();
    function->set_metaobject(&TimeLineView::staticMetaObject);
    function->set_description(tr("Fold Markers"));
    function->commandName = "FoldMarkers";
    function->useX = true;
    function->arguments << "fold_markers";
    add_function(function, MoveClipCommand);

    function = new TShortCutFunction();
    function->set_metaobject(&TrackView::staticMetaObject);
    function->set_description(tr("Move Up/Down"));
    function->commandName = "MoveTrack";
    function->useY = true;
    function->set_inherited_base("MoveBase");
    add_function(function, MoveTrackCommand);

    function = new TShortCutFunction();
    function->set_metaobject(&CurveView::staticMetaObject);
    function->set_description(tr("Move Curve Node(s)"));
    function->commandName = "MoveCurveNodes";
    function->useX = function->useY = true;
    function->set_inherited_base("MoveBase");
    add_function(function, MoveCurveNodesCommand);

    function = new TShortCutFunction();
    function->set_metaobject(&TAudioTrack::staticMetaObject);
    function->set_description(tr("Gain"));
    function->commandName = "AudioTrackGain";
    function->set_inherited_base("GainBase");
    add_function(function, GainCommand);

    function = new TShortCutFunction();
    function->set_metaobject(&TBusTrack::staticMetaObject);
    function->set_description(tr("Gain"));
    function->commandName = "BusTrackGain";
    function->set_inherited_base("GainBase");
    add_function(function, GainCommand);

    function = new TShortCutFunction();
    function->set_metaobject(&TSheetView::staticMetaObject);
    function->set_description(tr("Zoom"));
    function->commandName = "Zoom";
    function->useX = true;
    function->arguments << "HJogZoom" << "1.2" << "0.2";
    add_function(function, ZoomCommand);

    function = new TShortCutFunction();
    function->set_metaobject(&TimeLineView::staticMetaObject);
    function->set_description(tr("Move Marker"));
    function->commandName = "TimeLineMoveMarker";
    function->useX = true;
    function->set_inherited_base("MoveBase");
    add_function(function, MoveMarkerCommand);

    function = new TShortCutFunction();
    function->set_metaobject(&MarkerView::staticMetaObject);
    function->set_description(tr("Move Marker"));
    function->commandName = "MoveMarker";
    function->useX = true;
    function->set_inherited_base("MoveBase");
    add_function(function, MoveMarkerCommand);

    function = new TShortCutFunction();
    function->set_metaobject(&TTrack::staticMetaObject);
    function->set_description(tr("Track Pan"));
    function->commandName = "TrackPan";
    add_function(function, TrackPanCommand);


    function = new TShortCutFunction();
    function->set_metaobject(&TTrack::staticMetaObject);
    function->commandName = "RemoveTrack";
    function->set_inherited_base("DeleteBase");
    add_function(function, RemoveTrackCommand);

    function = new TShortCutFunction();
    function->set_metaobject(&TAudioPluginView::staticMetaObject);
    function->commandName = "RemovePlugin";
    function->set_inherited_base("DeleteBase");
    add_function(function, RemovePluginCommand);


    function = new TShortCutFunction();
    function->set_metaobject(&CurveView::staticMetaObject);
    function->commandName = "RemoveCurveNode";
    function->set_description("Remove Node(s)");
    function->set_inherited_base("DeleteBase");
    add_function(function, RemoveCurveNodeCommmand);

    function = new TShortCutFunction();
    function->set_metaobject(&TPanKnobView::staticMetaObject);
    function->set_description(tr("Panorama"));
    function->commandName = "PanKnobPanorama";
    add_function(function, TrackPanCommand);

    function = new TShortCutFunction();
    function->set_metaobject(&TAudioClipView::staticMetaObject);
    function->set_description(tr("Copy"));
    function->commandName = "CopyClip";
    function->arguments << "copy";
    function->useX = true;
    function->useY = true;
    add_function(function, MoveClipCommand);

    function = new TShortCutFunction();
    function->set_metaobject(&TAudioClipView::staticMetaObject);
    function->set_description(tr("Split"));
    function->commandName = "SplitClip";
    function->useX = true;
    add_function(function, SplitClipCommand);

    function = new TShortCutFunction();
    function->set_metaobject(&TAudioClipView::staticMetaObject);
    function->set_description(tr("Magnetic Cut"));
    function->commandName = "CropClip";
    function->useX = true;
    function->useY = true;
    add_function(function, CropClipCommand);


    function = new TShortCutFunction();
    function->set_metaobject(&TAudioClipView::staticMetaObject);
    function->set_description(tr("Move"));
    function->commandName = "MoveClip";
    function->arguments << "move";
    function->useX = true;
    function->useY = true;
    function->set_inherited_base("MoveBase");
    add_function(function, MoveClipCommand);

    function = new TShortCutFunction();
    function->set_metaobject(&TAudioClipView::staticMetaObject);
    function->set_description(tr("Move Edge"));
    function->commandName = "MoveClipEdge";
    function->arguments << "false";
    function->useX = true;
    add_function(function, MoveEdgeCommand);

    function = new TShortCutFunction();
    function->set_metaobject(&TAudioClip::staticMetaObject);
    function->set_description(tr("External Processing"));
    function->commandName = "AudioClipExternalProcessing";
    add_function(function, AudioClipExternalProcessingCommand);

    function = new TShortCutFunction();
    function->set_metaobject(&TAudioClip::staticMetaObject);
    function->set_description(tr("Gain"));
    function->commandName = "AudioClipGain";
    function->set_inherited_base("GainBase");
    add_function(function, GainCommand);

    function = new TShortCutFunction();
    function->set_metaobject(&TAudioClip::staticMetaObject);
    function->set_description(tr("Normalize Clip"));
    function->commandName = "NormalizeClip";
    add_function(function, NormalizeClipCommand);

    function = new TShortCutFunction();
    function->set_metaobject(&TAudioClip::staticMetaObject);
    function->set_description(tr("Remove AudioClip"));
    function->commandName = "RemoveClip";
    function->set_inherited_base("DeleteBase");
    add_function(function, RemoveClipCommand);

    function = new TShortCutFunction();
    function->set_metaobject(&TAudioClip::staticMetaObject);
    function->set_description(tr("(De)Select"));
    function->commandName = "ClipSelectionSelect";
    function->arguments << "toggle_selected";
    add_function(function, ClipSelectionCommand);

    function = new TShortCutFunction();
    function->set_metaobject(&TFadeCurveView::staticMetaObject);
    function->set_description(tr("Length"));
    function->commandName = "FadeLength";
    add_function(function, FadeRangeCommand);

    function = new TShortCutFunction();
    function->set_metaobject(&TAudioPluginView::staticMetaObject);
    function->set_description(tr("Move"));
    function->commandName = "MovePlugin";
    function->arguments << "false";
    function->useX = true;
    function->set_inherited_base("MoveBase");
    add_function(function, MovePluginCommand);

    function = new TShortCutFunction();
    function->set_metaobject(&TSheetView::staticMetaObject);
    function->set_description(tr("Fold Sheet"));
    function->commandName = "FoldSheet";
    function->arguments << "fold_sheet";
    function->useX = true;
    function->useY = true;
    add_function(function, MoveClipCommand);

    function = new TShortCutFunction();
    function->set_metaobject(&TSheetView::staticMetaObject);
    function->set_description(tr("Move Work Cursor"));
    function->commandName = "WorkCursorMove";
    add_function(function, WorkCursorMoveCommand);

    function = new TShortCutFunction();
    function->set_metaobject(&TSheetView::staticMetaObject);
    function->set_description(tr("Shuttle"));
    function->commandName = "Shuttle";
    function->useX = true;
    function->useY = true;
    add_function(function, ShuttleCommand);

    function = new TShortCutFunction();
    function->set_metaobject(&TAudioProcessingNode::staticMetaObject);
    function->set_description(tr("Gain Envelope"));
    function->commandName = "GainShowAutomation";
    add_function(function, GainShowAutomationCommand);


    // Moved from TShortCutManager.cpp to here:

    tShortCutManager().add_translation("ToggleBypassBase", tr("Toggle Bypass"));
    tShortCutManager().register_item_class("ToggleBypassBase", "ToggleBypassBase");

    tShortCutManager().add_translation("ToggleVerticalBase", tr("Toggle Vertical"));
    tShortCutManager().register_item_class("ToggleVerticalBase", "ToggleVerticalBase");

    tShortCutManager().add_translation("GainBase", tr("Gain"));
    tShortCutManager().register_item_class("GainBase", "GainBase");

    tShortCutManager().add_translation("DeleteBase", tr("Remove"));
    tShortCutManager().register_item_class("DeleteBase", "DeleteBase");

    tShortCutManager().add_translation("ResetBase", tr("Reset"));
    tShortCutManager().register_item_class("ResetBase", "ResetBase");

    tShortCutManager().add_translation("MoveBase", tr("Move"));
    tShortCutManager().register_item_class("MoveBase", "MoveBase");

    tShortCutManager().add_translation("EditPropertiesBase", tr("Edit Properties"));
    tShortCutManager().register_item_class("EditPropertiesBase", "EditPropertiesBase");

    function = new TShortCutFunction();
    function->set_metaobject(&ToggleVerticalBase::staticMetaObject);
    function->set_description(tr("Toggle Vertical"));
    function->commandName = "ToggleVerticalBase";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&GainBase::staticMetaObject);
    function->set_description(tr("Gain"));
    function->commandName = "GainBase";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&MoveBase::staticMetaObject);
    function->m_description = tr("Move");
    function->commandName = "MoveBase";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&DeleteBase::staticMetaObject);
    function->m_description = tr("Remove");
    function->commandName = "DeleteBase";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&ToggleBypassBase::staticMetaObject);
    function->slotsignature = "toggle_bypass";
    function->set_description(tr("Toggle Bypass"));
    function->commandName = "ToggleBypassBase";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&HoldCommand::staticMetaObject);
    function->slotsignature = "TMainWindow::show_context_menu";
    function->set_description(tr("Context Menu"));
    function->commandName = "HoldCommandShowContextMenu";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&ResetBase::staticMetaObject);
    function->slotsignature = "";
    function->set_description(tr("Reset"));
    function->commandName = "ResetBase";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TMoveCommand::staticMetaObject);
    function->slotsignature = "toggle_snap_on_off";
    function->set_description(tr("Toggle Snap on/off"));
    function->commandName = "MoveCommandToggleSnap";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&EditPropertiesBase::staticMetaObject);
    function->slotsignature = "edit_properties";
    function->set_description(tr("Edit Properties"));
    function->commandName = "EditPropertiesBase";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TAudioTrack::staticMetaObject);
    function->slotsignature = "toggle_arm";
    function->m_description = tr("Record: On/Off");
    function->commandName = "AudioTrackToggleRecord";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TAudioTrack::staticMetaObject);
    function->slotsignature = "silence_others";
    function->m_description = tr("Silence other tracks");
    function->commandName = "AudioTrackSilenceOthers";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TFadeCurve::staticMetaObject);
    function->slotsignature = "set_mode";
    function->m_description = tr("Cycle Shape");
    function->commandName = "FadeCurveCycleShape";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TMoveCommand::staticMetaObject);
    function->slotsignature = "move_right";
    function->m_description = tr("Move Right");
    function->commandName = "MoveCommandRight";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TMoveCommand::staticMetaObject);
    function->slotsignature = "move_left";
    function->m_description = tr("Move Left");
    function->commandName = "MoveCommandLeft";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TMoveCommand::staticMetaObject);
    function->slotsignature = "move_up";
    function->m_description = tr("Move Up");
    function->commandName = "MoveCommandUp";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TMoveCommand::staticMetaObject);
    function->slotsignature = "move_down";
    function->m_description = tr("Move Down");
    function->commandName = "MoveCommandDown";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TMoveCommand::staticMetaObject);
    function->slotsignature = "move_faster";
    function->m_description = tr("Move Faster");
    function->commandName = "MoveCommandFaster";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TMoveCommand::staticMetaObject);
    function->slotsignature = "move_slower";
    function->m_description = tr("Move Slower");
    function->commandName = "MoveCommandSlower";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TShortCutManager::staticMetaObject);
    function->slotsignature = "export_keymap";
    function->m_description = tr("Export keymap");
    function->commandName = "ExportShortcutMap";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TrackView::staticMetaObject);
    function->slotsignature = "add_new_plugin";
    function->m_description = tr("Add new Plugin");
    function->commandName = "TrackAddPlugin";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TPanKnobView::staticMetaObject);
    function->slotsignature = "pan_left";
    function->m_description = tr("Pan to Left");
    function->commandName = "PanKnobPanLeft";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TPanKnobView::staticMetaObject);
    function->slotsignature = "pan_right";
    function->m_description = tr("Pan to Right");
    function->commandName = "PanKnobPanRight";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&Zoom::staticMetaObject);
    function->slotsignature = "hzoom_out";
    function->set_description(tr("Out"));
    function->commandName = "ZoomOut";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&Zoom::staticMetaObject);
    function->slotsignature = "hzoom_in";
    function->set_description(tr("In"));
    function->commandName = "ZoomIn";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TrackPan::staticMetaObject);
    function->slotsignature = "pan_left";
    function->m_description = tr("Pan to Left");
    function->commandName = "TrackPanLeft";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TrackPan::staticMetaObject);
    function->slotsignature = "pan_right";
    function->m_description = tr("Pan to Right");
    function->commandName = "TrackPanRight";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TGainGroupCommand::staticMetaObject);
    function->slotsignature = "increase_gain";
    function->m_description = tr("Increase");
    function->commandName = "GainIncrease";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TGainGroupCommand::staticMetaObject);
    function->slotsignature = "decrease_gain";
    function->m_description = tr("Decrease");
    function->commandName = "GainDecrease";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&MoveTrack::staticMetaObject);
    function->slotsignature = "move_up";
    function->m_description = tr("Move Up");
    function->commandName = "MoveTrackUp";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&MoveTrack::staticMetaObject);
    function->slotsignature = "move_down";
    function->m_description = tr("Move Down");
    function->commandName = "MoveTrackDown";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TSheetView::staticMetaObject);
    function->slotsignature = "scroll_up";
    function->m_description =tr("Up");
    function->commandName = "ViewScrollUp";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TSheetView::staticMetaObject);
    function->slotsignature = "scroll_down";
    function->m_description = tr("Down");
    function->commandName = "ViewScrollDown";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&Zoom::staticMetaObject);
    function->slotsignature = "track_vzoom_out";
    function->m_description = tr("Track Vertical Zoom Out");
    function->commandName = "ZoomTrackVerticalOut";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&Zoom::staticMetaObject);
    function->slotsignature = "track_vzoom_in";
    function->m_description = tr("Track Vertical Zoom In");
    function->commandName = "ZoomTrackVerticalIn";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TSheetView::staticMetaObject);
    function->slotsignature = "to_upper_context_level";
    function->m_description = tr("One Layer Up");
    function->commandName = "NavigateToUpperContext";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TSheetView::staticMetaObject);
    function->slotsignature = "to_lower_context_level";
    function->m_description = tr("One Layer Down");
    function->commandName = "NavigateToLowerContext";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TAudioClipView::staticMetaObject);
    function->slotsignature = "fade_range";
    function->m_description = tr("Adjust Length");
    function->commandName = "AudioClipFadeLength";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TTransport::staticMetaObject);
    function->slotsignature = "start_transport";
    function->m_description = tr("Play (Start/Stop)");
    function->commandName = "TransportPlayStartStop";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TTransport::staticMetaObject);
    function->slotsignature = "set_recordable_and_start_transport";
    function->m_description = tr("Start Recording");
    function->commandName = "TransportSetRecordingPlayStart";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TTransport::staticMetaObject);
    function->slotsignature = "to_start";
    function->set_description(tr("To start"));
    function->commandName = "TransportToStart";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TTransport::staticMetaObject);
    function->slotsignature = "to_end";
    function->set_description(tr("To end"));
    function->commandName = "TransportToEnd";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TrackView::staticMetaObject);
    function->set_inherited_base("EditPropertiesBase");
    function->commandName = "EditTrackProperties";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TSheetView::staticMetaObject);
    function->set_inherited_base("EditPropertiesBase");
    function->commandName = "EditSongProperties";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TAudioPluginView::staticMetaObject);
    function->set_inherited_base("EditPropertiesBase");
    function->commandName = "EditPluginProperties";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&SpectralMeterView::staticMetaObject);
    function->set_inherited_base("EditPropertiesBase");
    function->commandName = "EditSpectralMeterProperties";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TAudioClipView::staticMetaObject);
    function->set_inherited_base("EditPropertiesBase");
    function->commandName = "EditAudioClipProperties";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&Zoom::staticMetaObject);
    function->slotsignature = "toggle_expand_all_tracks";
    function->m_description = tr("Expand/Collapse Tracks");
    function->commandName = "ZoomToggleExpandAllTracks";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TrackPanelLed::staticMetaObject);
    function->slotsignature = "toggle";
    function->m_description = tr("Toggle On/Off");
    function->commandName = "PanelLedToggle";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&CurveView::staticMetaObject);
    function->slotsignature = "add_node";
    function->m_description = tr("New Node");
    function->commandName = "AddCurveNode";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TMoveCommand::staticMetaObject);
    function->slotsignature = "numerical_input";
    function->m_description = tr("Moving Speed");
    function->commandName = "MoveCommandSpeed";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TCommand::staticMetaObject);
    function->m_description = tr("Reject");
    function->commandName = "RejectHoldCommand";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TCommand::staticMetaObject);
    function->m_description = tr("Accept");
    function->commandName = "AcceptHoldCommand";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&Zoom::staticMetaObject);
    function->slotsignature = "numerical_input";
    function->m_description = tr("Track Height");
    function->commandName = "ZoomNumericalInput";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TFadeCurveView::staticMetaObject);
    function->slotsignature = "select_fade_shape";
    function->m_description = tr("Select Preset");
    function->commandName = "FadeSelectPreset";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TSheetView::staticMetaObject);
    function->slotsignature = "browse_to_time_line";
    function->m_description = tr("To Timeline");
    function->commandName = "NavigateToTimeLine";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TrackPanelGain::staticMetaObject);
    function->slotsignature = "gain_decrement";
    function->m_description = tr("Decrease");
    function->commandName = "TrackPanelGainDecrement";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TrackPanelGain::staticMetaObject);
    function->slotsignature = "gain_increment";
    function->m_description = tr("Increase");
    function->commandName = "TrackPanelGainIncrement";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&CropClip::staticMetaObject);
    function->slotsignature = "adjust_left";
    function->m_description = tr("Adjust Left");
    function->commandName = "CropClipAdjustLeft";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&CropClip::staticMetaObject);
    function->slotsignature = "adjust_right";
    function->m_description = tr("Adjust Right");
    function->commandName = "CropClipAdjustRight";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TSheetView::staticMetaObject);
    function->slotsignature = "touch";
    function->m_description = tr("Set");
    function->commandName = "WorkCursorTouch";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TimeLineView::staticMetaObject);
    function->slotsignature = "playhead_to_marker";
    function->m_description = tr("Playhead to Marker");
    function->commandName = "TimeLinePlayheadToMarker";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TAudioClipView::staticMetaObject);
    function->slotsignature = "set_audio_file";
    function->m_description = tr("Reset Audio File");
    function->commandName = "AudioClipSetAudioFile";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TFadeCurve::staticMetaObject);
    function->slotsignature = "toggle_raster";
    function->m_description = tr("Toggle Raster");
    function->commandName = "FadeCurveToggleRaster";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&SpectralMeterView::staticMetaObject);
    function->slotsignature = "reset";
    function->m_description = tr("Reset average curve");
    function->commandName = "SpectralMeterResetAverageCurve";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TAudioClip::staticMetaObject);
    function->slotsignature = "lock";
    function->m_description = tr("Lock");
    function->commandName = "AudioClipLock";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&CorrelationMeterView::staticMetaObject);
    function->slotsignature = "set_mode";
    function->m_description = tr("Toggle display range");
    function->commandName = "CorrelationMeterToggleDisplayRange";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&SpectralMeterView::staticMetaObject);
    function->slotsignature = "set_mode";
    function->m_description = tr("Toggle average curve");
    function->commandName = "SpectralMeterToggleDisplayRange";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TimeLineView::staticMetaObject);
    function->slotsignature = "add_marker";
    function->m_description = tr("Add Marker");
    function->commandName = "TimeLineAddMarker";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TSheetView::staticMetaObject);
    function->slotsignature = "add_marker";
    function->m_description = tr("Add Marker");
    function->commandName = "SheetAddMarker";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TSheetView::staticMetaObject);
    function->slotsignature = "add_marker_at_playhead";
    function->m_description = tr("Add Marker at Playhead");
    function->commandName = "SheetAddMarkerAtPlayhead";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TimeLineView::staticMetaObject);
    function->slotsignature = "add_marker_at_playhead";
    function->m_description = tr("Add Marker at Playhead");
    function->commandName = "TimeLineAddMarkerAtPlayhead";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TSheetView::staticMetaObject);
    function->slotsignature = "add_marker_at_work_cursor";
    function->m_description = tr("Add Marker at Work Cursor");
    function->commandName = "SheetAddMarkerAtWorkCursor";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TimeLineView::staticMetaObject);
    function->slotsignature = "add_marker_at_work_cursor";
    function->m_description = tr("Add Marker at Work Cursor");
    function->commandName = "TimeLineAddMarkerAtWorkCursor";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TFadeCurve::staticMetaObject);
    function->set_inherited_base("ToggleBypassBase");
    function->commandName = "FadeCurveToggleBypass";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TAudioPlugin::staticMetaObject);
    function->set_inherited_base("ToggleBypassBase");
    function->commandName = "PluginToggleBypass";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TFadeCurveView::staticMetaObject);
    function->slotsignature = "bend";
    function->set_description(tr("Adjust Bend"));
    function->useY = true;
    function->commandName = "FadeCurveBend";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TimeLineView::staticMetaObject);
    function->slotsignature = "TMainWindow::show_marker_dialog";
    function->set_description(tr("Edit Markers"));
    function->commandName = "TimeLineShowMarkerDialog";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TAudioProcessingNode::staticMetaObject);
    function->slotsignature = "mute";
    function->set_description(tr("Mute"));
    function->commandName = "Mute";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TTrack::staticMetaObject);
    function->slotsignature = "solo";
    function->set_description(tr("Solo"));
    function->commandName = "Solo";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&CurveView::staticMetaObject);
    function->slotsignature = "toggle_select_all_nodes";
    function->set_description(tr("Select All Nodes"));
    function->commandName = "CurveSelectAllNodes";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TProjectManager::staticMetaObject);
    function->slotsignature = "save_project";
    function->set_description(tr("Save Project"));
    function->commandName = "ProjectSave";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&CurveView::staticMetaObject);
    function->slotsignature = "select_lazy_selected_node";
    function->set_description(tr("Select Node"));
    function->commandName = "CurveSelectNode";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TFadeCurveView::staticMetaObject);
    function->slotsignature = "strength";
    function->set_description(tr("Adjust Strength"));
    function->commandName = "FadeCurveStrenght";
    function->useX = true;
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TSheetView::staticMetaObject);
    function->slotsignature = "goto_end";
    function->set_description(tr("To end"));
    function->commandName = "WorkCursorToEnd";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&MoveTrack::staticMetaObject);
    function->slotsignature = "to_bottom";
    function->set_description(tr("To Bottom"));
    function->commandName = "MoveTrackToBottom";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TSheetView::staticMetaObject);
    function->slotsignature = "goto_begin";
    function->set_description(tr("To start"));
    function->commandName = "WorkCursorToStart";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&MoveTrack::staticMetaObject);
    function->slotsignature = "to_top";
    function->set_description(tr("To Top"));
    function->commandName = "MoveTrackToTop";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&PlayHeadMove::staticMetaObject);
    function->slotsignature = "move_to_work_cursor";
    function->set_description(tr("To Work Cursor"));
    function->commandName = "PlayHeadMoveToWorkCursor";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&PlayHeadMove::staticMetaObject);
    function->slotsignature = "move_to_start";
    function->set_description(tr("To Start"));
    function->commandName = "PlayHeadMoveToStart";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TSheetView::staticMetaObject);
    function->slotsignature = "touch_play_cursor";
    function->set_description(tr("Set"));
    function->commandName = "SheetSetPlayPosition";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TSheetView::staticMetaObject);
    function->slotsignature = "center_playhead";
    function->set_description(tr("Center"));
    function->commandName = "SheetCenterPlayhead";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&Zoom::staticMetaObject);
    function->slotsignature = "toggle_vertical_horizontal_jog_zoom";
    function->set_description(tr("Toggle Vertical / Horizontal"));
    function->commandName = "ZoomToggleVerticalHorizontal";
    function->set_inherited_base("ToggleVerticalBase");
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&MoveClip::staticMetaObject);
    function->slotsignature = "toggle_vertical_only";
    function->set_description(tr("Toggle Vertical Only"));
    function->commandName = "MoveClipToggleVerticalOnly";
    function->set_inherited_base("ToggleVerticalBase");
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&MoveCurveNode::staticMetaObject);
    function->slotsignature = "toggle_vertical_only";
    function->set_description(tr("Toggle Vertical Only"));
    function->commandName = "MoveCurveNodeToggleVerticalOnly";
    function->set_inherited_base("ToggleVerticalBase");
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&WorkCursorMove::staticMetaObject);
    function->slotsignature = "move_to_play_cursor";
    function->set_description(tr("To Playhead"));
    function->commandName = "WorkCursorMoveToPlayhead";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TAudioTrackView::staticMetaObject);
    function->slotsignature = "insert_silence";
    function->set_description(tr("Insert Silence"));
    function->commandName = "AudioTrackInsertSilence";
    tShortCutManager().register_shortcut_function(function);


    function = new TShortCutFunction();
    function->set_metaobject(&TrackPan::staticMetaObject);
    function->slotsignature = "reset_pan";
    function->set_description(tr("Reset"));
    function->commandName = "TrackPanReset";
    function->set_inherited_base("ResetBase");
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&FadeRange::staticMetaObject);
    function->slotsignature = "reset_length";
    function->set_description(tr("Reset"));
    function->commandName = "FadeResetLength";
    function->set_inherited_base("ResetBase");
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TGainGroupCommand::staticMetaObject);
    function->slotsignature = "numerical_input";
    function->set_description(tr("Input dB value"));
    function->commandName = "GainNumericalInput";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TGainGroupCommand::staticMetaObject);
    function->slotsignature = "toggle_primary_gain_only";
    function->set_description(tr("Toggle Selection"));
    function->commandName = "GainToggleSelection";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TGainGroupCommand::staticMetaObject);
    function->slotsignature = "reset_gain";
    function->set_description(tr("Reset"));
    function->commandName = "GainReset";
    function->set_inherited_base("ResetBase");
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TMainWindow::staticMetaObject);
    function->slotsignature = "browse_to_first_track_in_active_sheet";
    function->set_description(tr("Browse to first Track in current View"));
    function->commandName = "MainWindowNavigateToFirstTrack";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TMainWindow::staticMetaObject);
    function->slotsignature = "browse_to_last_track_in_active_sheet";
    function->set_description(tr("Browse to last Track in current View"));
    function->commandName = "MainWindowNavigateToLastTrack";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&WorkCursorMove::staticMetaObject);
    function->slotsignature = "move_to_start";
    function->set_description(tr("To Start"));
    function->commandName = "WorkCursorMoveToStart";
    tShortCutManager().register_shortcut_function(function);

    function->set_metaobject(&TMainWindow::staticMetaObject);
    function->slotsignature = "show_track_finder";
    function->set_description(tr("Activate Track Finder"));
    function->commandName = "MainWindowActivateTrackFinder";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TMainWindow::staticMetaObject);
    function->set_slot_signature("set_transport_location");
    function->set_description(tr("Set Play Position"));
    function->commandName = "TransportSetPosition";
    function->useX = true;
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TMainWindow::staticMetaObject);
    function->slotsignature = "quick_start";
    function->m_description = tr("Show Help");
    function->commandName = "ShowHelp";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TMainWindow::staticMetaObject);
    function->slotsignature = "full_screen";
    function->m_description = tr("Full Screen");
    function->commandName = "MainWindowShowFullScreen";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TMainWindow::staticMetaObject);
    function->slotsignature = "show_project_manager_dialog";
    function->set_description(tr("Show Project Management Dialog"));
    function->commandName = "MainWindowShowProjectManagementDialog";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TMainWindow::staticMetaObject);
    function->slotsignature = "show_context_menu";
    function->set_description(tr("Context Menu"));
    function->commandName = "ShowContextMenu";
    tShortCutManager().register_shortcut_function(function);

    function = new TShortCutFunction();
    function->set_metaobject(&TMainWindow::staticMetaObject);
    function->slotsignature = "show_newtrack_dialog";
    function->m_description = tr("New Track Dialog");
    function->commandName = "ShowNewTrackDialog";
    tShortCutManager().register_shortcut_function(function);

}

void TraversoCommands::add_function(TShortCutFunction *function, TraversoCommand command)
{
    function->pluginname = "TraversoCommands";
    m_dict.insert(function->commandName, command);
    tShortCutManager().register_shortcut_function(function);
}

TCommand* TraversoCommands::create(QObject* obj, const QString& commandName, QVariantList arguments)
{
    switch (m_dict.value(commandName)) {
    case GainCommand:
    {
        TContextItem* contextItem = qobject_cast<TContextItem*>(obj);
        Q_ASSERT(contextItem);

        if (contextItem->metaObject()->className() == QString("TrackPanelGain")) {
            contextItem = contextItem->get_related_context_item();
        } else if (TAudioClipView* view = qobject_cast<TAudioClipView*>(contextItem)) {
            contextItem = view->get_related_context_item();
        } else if (TrackView* view = qobject_cast<TrackView*>(contextItem)) {
            contextItem = view->get_related_context_item();
        }


        if (!contextItem) {
            PERROR("TraversoCommands: Supplied QObject was not a ContextItem, "
                   "GainCommand only works with ContextItem objects!!");
            return nullptr;
        }

        auto group = new TGainGroupCommand(contextItem, arguments);

        TAudioClip* clip = qobject_cast<TAudioClip*>(contextItem);
        if (clip && clip->is_selected()) {

            QList<TAudioClip* > selection;
            clip->get_sheet()->get_audioclip_manager()->get_selected_clips(selection);

            // always the contextitem first so it will be the primary gain object
            group->add_command(new Gain(contextItem, arguments));

            for(auto audioProcessingNode : selection) {
                // only add the context item once
                if (audioProcessingNode == contextItem) {
                    continue;
                }
                group->add_command(new Gain(audioProcessingNode, arguments));
            }
        } else {
            group->add_command(new Gain(contextItem, arguments));
        }


        return group;
    }

    case TrackPanCommand:
    {
        TTrack* track = qobject_cast<TTrack*>(obj);
        if (! track) {
            TPanKnobView* knob = qobject_cast<TPanKnobView*>(obj);
            if(knob) {
                track = knob->get_track();
            }
            if (!track) {
                PERROR("TraversoCommands: Supplied QObject was not a Track! "
                       "TrackPanCommand needs a Track as argument");
                return nullptr;
            }
        }
        return new TrackPan(track, arguments);
    }

    case ImportAudioCommand:
    {
        TAudioTrack* track = qobject_cast<TAudioTrack*>(obj);
        if (! track) {
            PERROR("TraversoCommands: Supplied QObject was not a Track! "
                   "ImportAudioCommand needs a Track as argument");
            return nullptr;
        }

        auto audioFileImportCommand = new TAudioFileImportCommand(track);
        audioFileImportCommand->set_track(track);
        return audioFileImportCommand;
    }

    case InsertSilenceCommand:
    {
        TAudioTrack* track = qobject_cast<TAudioTrack*>(obj);
        if (! track) {
            PERROR("TraversoCommands: Supplied QObject was not a Track! "
                   "ImportAudioCommand needs a Track as argument");
            return nullptr;
        }
        TTimeRef length(10*TTimeRef::UNIVERSAL_SAMPLE_RATE);
        auto audioFileImportCommand = new TAudioFileImportCommand(track);
        audioFileImportCommand->set_track(track);
        audioFileImportCommand->set_length(length);
        audioFileImportCommand->set_silent(true);
        return audioFileImportCommand;
    }

    case AddNewAudioTrackCommand:
    {
        TSheet* sheet = qobject_cast<TSheet*>(obj);
        if (!sheet) {
            PERROR("TraversoCommands: Supplied QObject was not a Sheet! "
                   "AddNewAudioTrackCommand needs a Sheet as argument");
            return nullptr;
        }
        return sheet->add_track(new TAudioTrack(sheet, "Unnamed", TAudioTrack::INITIAL_HEIGHT));
    }

    case RemoveClipCommand:
    {
        TAudioClip* clip = qobject_cast<TAudioClip*>(obj);
        if (!clip) {
            PERROR("TraversoCommands: Supplied QObject was not a Clip! "
                   "RemoveClipCommand needs a Clip as argument");
            return nullptr;
        }
        return new AddRemoveClip(clip, AddRemoveClip::REMOVE);
    }

    case RemoveTrackCommand:
    {
        TTrack* track = qobject_cast<TTrack*>(obj);
        if (!track) {
            PERROR("TraversoCommands: Supplied QObject was not a Track! "
                   "RemoveTrackCommand needs a Track as argument");
            return nullptr;
        }

        TSession* activeSession = pm().get_project()->get_current_session();
        if (!activeSession) {
            // this is rather impossible!!
            tInformUser().information(tr("Removing Track %1, but no active (Work) Sheet ??").arg(track->get_name()));
            return nullptr;
        }

        if (track == activeSession->get_master_out_bus_track()) {
            tInformUser().information(tr("It is not possible to remove the Master Out track!"));
            return nullptr;
        }
        return activeSession->remove_track(track);
    }

    case RemovePluginCommand:
    {
        TAudioPluginView* view = qobject_cast<TAudioPluginView*>(obj);
        if (!view) {
            PERROR("TraversoCommands: Supplied QObject was not a PluginView! "
                   "RemovePluginCommand needs a PluginView as argument");
            return nullptr;
        }

        return view->remove_plugin();
    }

    case RemoveCurveNodeCommmand:
    {
        CurveView* curveView = qobject_cast<CurveView*>(obj);
        if (!curveView)
        {
            PERROR("TraversoCommands: Supplied QObject was not a CurveView! "
                   "RemoveClipNodeCommmand needs a CurveView as argument");
            return nullptr;
        }
        return curveView->remove_node();

    }
    case AudioClipExternalProcessingCommand:
    {
        TAudioClip* clip = qobject_cast<TAudioClip*>(obj);
        if (!clip) {
            PERROR("TraversoCommands: Supplied QObject was not an AudioClip! "
                   "AudioClipExternalProcessingCommand needs an AudioClip as argument");
            return nullptr;
        }
        return new AudioClipExternalProcessing(clip);
    }

    case ClipSelectionCommand:
    {
        TSheet* sheet = qobject_cast<TSheet*>(obj);
        if (sheet) {
            QString action;
            if (!arguments.empty()) {
                action = arguments.at(0).toString();
                if (action == "select_all_clips") {
                    return sheet->get_audioclip_manager()->select_all_clips();
                }
            }
        }
        TAudioClip* clip = qobject_cast<TAudioClip*>(obj);
        if (!clip) {
            PERROR("TraversoCommands: Supplied QObject was not an AudioClip! "
                   "ClipSelectionCommand needs an AudioClip as argument");
            return nullptr;
        }

        // audio clip selection doesn't support/need number collection, but
        // other commands do, so if ie() has number collection, ignore it for this clip
        if (ied().has_collected_number()) {
            return ied().did_not_implement();
        }

        return new ClipSelection(clip, arguments);
    }

    case MoveClipCommand:
    {
        // cast to super class ViewItem since we use MoveClip also to fold Track or Sheet
        ViewItem* view = qobject_cast<ViewItem*>(obj);
        if (view) {
            return new MoveClip(view, arguments);
        }
        PERROR("TraversoCommands: Supplied QObject was not a ViewItem, MoveClipCommand needs a ViewItem as argument");
        return nullptr;
    }

    case MoveTrackCommand:
    {
        TrackView* view = qobject_cast<TrackView*>(obj);

        if (!view) {
            PERROR("TraversoCommands: Supplied QObject was not an TrackView! "
                   "MoveTrackCommand needs an TrackView as argument");
            return nullptr;
        }

        return new MoveTrack(view);
    }

    case MovePluginCommand:
    {
        TAudioPluginView* view = qobject_cast<TAudioPluginView*>(obj);
        if (view) {
            return new MovePlugin(view);
        }
        return nullptr;
    }


    case MoveEdgeCommand:
    {
        TAudioClipView* view = qobject_cast<TAudioClipView*>(obj);
        if (!view) {
            PERROR("TraversoCommands: Supplied QObject was not an AudioClipView! "
                   "MoveEdgeCommand needs an AudioClipView as argument");
            return nullptr;
        }

        int x = (int) (cpointer().on_first_input_event_scene_x() - view->scenePos().x());

        if (x < (view->boundingRect().width() / 2)) {
            return new MoveEdge(view, view->get_sheetview(), "set_left_edge");
        } else {
            return new MoveEdge(view, view->get_sheetview(), "set_right_edge");
        }
    }

        // The existence of this is doubtfull. Using [ E ] is so much easier
        // then trying to mimic 'if near to edge, drag edge' features.
    case MoveClipOrEdgeCommand:
    {
        TAudioClipView* view = qobject_cast<TAudioClipView*>(obj);

        if (!view) {
            PERROR("TraversoCommands: Supplied QObject was not an AudioClipView! "
                   "MoveClipOrEdgeCommand needs an AudioClipView as argument");
            return nullptr;
        }

        int x = (int) (cpointer().on_first_input_event_scene_x() - view->scenePos().x());

        int edge_width = 0;
        if (arguments.size() == 2) {
            edge_width = arguments[0].toInt();
        }

        if (x < edge_width) {
            return new MoveEdge(view, view->get_sheetview(), "set_left_edge");
        } else if (x > (view->boundingRect().width() - edge_width)) {
            return new MoveEdge(view, view->get_sheetview(), "set_right_edge");
        }

        return new MoveClip(view, QVariantList() << "move");
    }

    case SplitClipCommand:
    {
        TAudioClipView* view = qobject_cast<TAudioClipView*>(obj);
        if (!view) {
            PERROR("TraversoCommands: Supplied QObject was not an AudioClipView! "
                   "SplitClipCommand needs an AudioClipView as argument");
            return nullptr;
        }
        return new SplitClip(view);
    }

    case CropClipCommand:
    {
        TAudioClipView* view = qobject_cast<TAudioClipView*>(obj);
        if (!view) {
            PERROR("TraversoCommands: Supplied QObject was not an AudioClipView! "
                   "CropClipCommand needs an AudioClipView as argument");
            return nullptr;
        }
        return new CropClip(view);
    }

    case ArmTracksCommand:
    {
        TSheetView* view = qobject_cast<TSheetView*>(obj);
        if (!view) {
            PERROR("TraversoCommands: Supplied QObject was not an SheetView! "
                   "ArmTracksCommand needs an SheetView as argument");
            return nullptr;
        }
        return new ArmTracks(view);
    }

    case ZoomCommand:
    {
        TSheetView* view = qobject_cast<TSheetView*>(obj);
        if (!view) {
            PERROR("TraversoCommands: Supplied QObject was not an SheetView! "
                   "ZoomCommand needs an SheetView as argument");
            return nullptr;

        }
        return new Zoom(view, arguments);
    }

    case WorkCursorMoveCommand:
    {
        TSheetView* view = qobject_cast<TSheetView*>(obj);
        if (!view) {
            PERROR("TraversoCommands: Supplied QObject was not an SheetView! "
                   "WorkCursorMove Command needs an SheetView as argument");
            return nullptr;
        }
        return new WorkCursorMove(view);
    }

    case MoveCurveNodesCommand:
    {
        CurveView* curveView = qobject_cast<CurveView*>(obj);
        if (!curveView) {
            return nullptr;
        }

        return curveView->drag_node();
    }

    case ArrowKeyBrowserCommand:
    {
        TSheetView* view = qobject_cast<TSheetView*>(obj);
        if (!view) {
            PERROR("TraversoCommands: Supplied QObject was not an SheetView! "
                   "ArrowKeyBrowserCommand needs an SheetView as argument");
            return nullptr;
        }
        return new ArrowKeyBrowser(view, arguments);
    }

    case ShuttleCommand:
    {
        TSheetView* view = qobject_cast<TSheetView*>(obj);
        if (view) {
            return new TMoveCommand(view, nullptr, "");
        }
        return nullptr;
    }

    case NormalizeClipCommand:
    {
        TAudioClip* clip = qobject_cast<TAudioClip*>(obj);
        if (!clip) {
            PERROR("TraversoCommands: Supplied QObject was not a Clip! "
                   "RemoveClipCommand needs a Clip as argument");
            return nullptr;
        }

        if (clip->is_selected()) {
            bool ok;
            float normfactor = FLT_MAX;

            double d = QInputDialog::getDouble(0, tr("Normalization"),
                                               tr("Set Normalization level:"), 0.0, -120, 0, 1, &ok);

            if (!ok) {
                return nullptr;
            }
            QList<TAudioClip* > selection;
            clip->get_sheet()->get_audioclip_manager()->get_selected_clips(selection);
            foreach(TAudioClip* selected, selection) {
                normfactor = std::min(selected->calculate_normalization_factor(d), normfactor);
            }

            CommandGroup* group = new CommandGroup(clip, tr("Normalize Selected Clips"));

            foreach(TAudioClip* selected, selection) {
                group->add_command(new PCommand(selected, "set_gain", normfactor, selected->get_gain(), tr("AudioClip: Normalize")));
            }

            return group;
        }

        return clip->normalize();
    }
    case MoveMarkerCommand:
    {
        TimeLineView* view = qobject_cast<TimeLineView*>(obj);
        if (view)
        {
            return view->drag_marker();
        }

        MarkerView* markerView = qobject_cast<MarkerView*>(obj);
        if (markerView)
        {
            return markerView->drag_marker();
        }

        PERROR("TraversoCommands: Supplied QObject was not a TimeLineView or MarkerView! "
               "MoveMarkerCommand needs a TimeLineView or MarkerView as argument");
        return nullptr;
    }
    case FadeRangeCommand:
    {
        TFadeCurveView* view = qobject_cast<TFadeCurveView*>(obj);
        if (view) {
            return new FadeRange(view->get_audio_clip(), view->get_fade(), view->get_sheetview()->timeref_scalefactor);
        }
        return nullptr;
    }
    case GainShowAutomationCommand:
    {
        TAudioClip* audioClip = qobject_cast<TAudioClip*>(obj);
        if (audioClip) {
            audioClip->get_track()->toggle_show_clip_volume_automation();
            return ied().succes();
        }
        TTrack* track = qobject_cast<TTrack*>(obj);
        if (track) {
            track->toggle_show_gain_automation_curve();
            return ied().succes();
        }
        return ied().failure();
    }

    }

    return ied().did_not_implement();
}

// eof
