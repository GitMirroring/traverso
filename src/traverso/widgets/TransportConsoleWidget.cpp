/*
    Copyright (C) 2024 Remon Sijrier
    Copyright (C) 2008 Nicola Doebelin

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

#include "TransportConsoleWidget.h"

#include "TAudioDevice.h"
#include "TSheet.h"
#include "Utils.h"
#include "TProjectManager.h"
#include "TProject.h"
#include "TConfig.h"
#include "TInformUser.h"
#include "TTransport.h"

#include <QAction>
#include <QPushButton>
#include <QFont>
#include <QString>

TransportConsoleWidget::TransportConsoleWidget(QWidget* parent)
	: QToolBar(parent)
{
    setEnabled(false);

    m_project = nullptr;
    m_sheet = nullptr;

    m_transportLocation = TTimeRef();
    m_lastTransportLocationUpdatetime = 0;

    m_timeLabel = new QPushButton(this);
    m_timeLabel->setFocusPolicy(Qt::NoFocus);
    m_timeLabel->setStyleSheet(
        "color: lime;"
        "background-color: black;"
        "font: 19px;"
        "border: 2px solid gray;"
        "border-radius: 10px;"
        "padding: 0 20 0 20;");

    m_toStartAction = addAction(QIcon(":/skipleft"), tr("Skip to Start"));
    connect(m_toStartAction, &QAction::triggered, &transport(), &TTransport::to_start);
    m_toLeftAction = addAction(QIcon(":/seekleft"), tr("Previous Snap Position"));
    connect(m_toLeftAction, &QAction::triggered, &transport(), &TTransport::prev_skip_pos);
    m_recAction = addAction(QIcon(":/record"), tr("Record"));
    connect(m_recAction, &QAction::triggered, this, &TransportConsoleWidget::rec_toggled);
    m_playAction = addAction(QIcon(":/playstart"), tr("Play / Stop"));
    connect(m_playAction, &QAction::triggered, &transport(), &TTransport::start_transport);
    m_toRightAction = addAction(QIcon(":/seekright"), tr("Next Snap Position"));
    connect(m_toRightAction, &QAction::triggered, &transport(), &TTransport::next_skip_pos);
    m_toEndAction = addAction(QIcon(":/skipright"), tr("Skip to End"));
    connect(m_toEndAction, &QAction::triggered, &transport(), &TTransport::to_end);
    m_freeWheelingAction = addAction("RT", tr("Start/Stop FreeWheeling"), this, [](){
        audiodevice().set_free_wheeling(audiodevice().running_real_time());
    });
    m_freeWheelingAction->setCheckable(true);
    m_freeWheelingAction->setToolTip(tr("Turn Free Wheeling ON"));

    addWidget(m_timeLabel);

    m_recAction->setCheckable(true);
    m_playAction->setCheckable(true);

    m_lastSnapPosition = TTimeRef();

    connect(&pm(), &TProjectManager::projectLoaded, this, &TransportConsoleWidget::set_project);
    connect(&audiodevice(), &TAudioDevice::finishedOneProcessCycle, this, &TransportConsoleWidget::update_label);
    connect(&audiodevice(), &TAudioDevice::freeWheelingChanged, this, [this](){
        m_freeWheelingAction->setChecked(!audiodevice().running_real_time());
        if (audiodevice().running_real_time()) {
            m_freeWheelingAction->setText("RT");
            m_freeWheelingAction->setToolTip(tr("Turn Free Wheeling ON"));
        } else {
            m_freeWheelingAction->setText("FW");
            m_freeWheelingAction->setToolTip(tr("Turn Free Wheeling OFF"));
        }
    });

    update_layout();
    update_label();
}


void TransportConsoleWidget::set_project(TProject* project)
{
    if (m_project) {
        disconnect(m_project, &TProject::currentSessionChanged, this, &TransportConsoleWidget::set_session);
    }

    m_project = project;

    if (m_project) {
        connect(m_project, &TProject::currentSessionChanged, this, &TransportConsoleWidget::set_session);
    } else {
        set_session(nullptr);
    }
}

void TransportConsoleWidget::set_session(TSession* session)
{
    TProject* project = qobject_cast<TProject*>(session);
    // if the view was changed to Project's session (mixer)
    // then keep the current active sheet!
    if (project) {
        return;
    }

    if (m_sheet) {
        disconnect(m_sheet, &TSheet::recordingStateChanged, this, &TransportConsoleWidget::update_recording_state);
        disconnect(m_sheet, &TSheet::transportStarted, this, &TransportConsoleWidget::transport_started);
        disconnect(m_sheet, &TSheet::transportStopped, this, &TransportConsoleWidget::transport_stopped);

    }

    m_sheet = qobject_cast<TSheet*>(session);
    if (!m_sheet && session) {
        m_sheet = qobject_cast<TSheet*>(session->get_parent_session());
    }

    if (!m_sheet) {
        setEnabled(false);
        update_label();
        return;
    }

    setEnabled(true);

    connect(m_sheet, &TSheet::recordingStateChanged, this, &TransportConsoleWidget::update_recording_state);
    connect(m_sheet, &TSheet::transportStarted, this, &TransportConsoleWidget::transport_started);
    connect(m_sheet, &TSheet::transportStopped, this, &TransportConsoleWidget::transport_stopped);
}

void TransportConsoleWidget::rec_toggled()
{
    m_sheet->set_recordable();
}

void TransportConsoleWidget::transport_started()
{
    m_playAction->setChecked(true);
    m_playAction->setIcon(QIcon(":/playstop"));
    m_recAction->setEnabled(false);

	// this is needed when the record button is pressed, but no track is armed.
	// uncheck the rec button in that case
    if (m_sheet && !m_sheet->is_recording()) {
        m_recAction->setChecked(false);
    }
}

void TransportConsoleWidget::transport_stopped()
{
    m_playAction->setChecked(false);
    m_playAction->setIcon(QIcon(":/playstart"));
    m_recAction->setEnabled(true);
}

void TransportConsoleWidget::update_recording_state()
{
	if (!m_sheet)
	{
		return;
	}

    if (m_sheet->is_recording()) {
        QString recordFormat = config().get_property("Recording", "FileFormat", "wav").toString();
        tInformUser().information(tr("Recording to %1 Tracks, encoding format: %2").arg(m_sheet->get_armed_tracks().size()).arg(recordFormat));
        m_recAction->setChecked(true);
    } else {
        m_recAction->setChecked(false);
    }
}

void TransportConsoleWidget::update_label()
{
    auto newUpdateTime = TTimeRef::get_milliseconds_since_epoch();

    // Limit the updating of the label to 8 frames/sec
    // Easier on the eyes
    // And drawing text in Qt is very CPU intensive
    if ((newUpdateTime - m_lastTransportLocationUpdatetime) < 125) {
        return;
    }

    m_lastTransportLocationUpdatetime = newUpdateTime;

    TTimeRef newTransportLocation(TTimeRef::INVALID);

    if (!m_sheet) {
        if (m_transportLocation != TTimeRef::INVALID) {
            m_transportLocation = TTimeRef::INVALID;
            m_timeLabel->setText(TTimeRef::timeref_to_ms_2(m_transportLocation));
        }
    } else {
        newTransportLocation = m_sheet->get_transport_location();
    }


    if (m_transportLocation == newTransportLocation) {
        return;
    }

    m_transportLocation = newTransportLocation;

    m_timeLabel->setText(TTimeRef::timeref_to_ms_2(m_transportLocation));
}

void TransportConsoleWidget::update_layout()
{
	int iconsize = config().get_property("Themer", "transportconsolesize", "22").toInt();
	setIconSize(QSize(iconsize, iconsize));
}

//eof

