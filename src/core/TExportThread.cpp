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
 
    $Id: Export.cpp,v 1.18 2009/05/07 19:59:03 n_doebelin Exp $
*/

#include "TExportThread.h"
#include "AudioDevice.h"
#include "Information.h"
#include "Sheet.h"
#include "TExportSpecification.h"
#include "Project.h"

// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"
#include "Utils.h"
#include <cfloat>

TExportThread::TExportThread(Project* project)
	: QThread(project)
{
    m_project = project;
}

void TExportThread::set_specification(TExportSpecification * spec)
{
    m_exportSpecification  = spec;
    m_exportSpecification->thread = this;
}


void TExportThread::run( )
{
    // FIXME
    // used to be default export block size as set in TExportSpecification (16 KB)
    // this is more efficient then the audio device buffer size, are buffers not
    // correctly resized on export perhaps and this hack was applied to make export work again ?
    // m_exportSpecification->set_block_size(audiodevice().get_buffer_size());

    int overallExportProgress;
    int renderedSheets = 0;
    QList<Sheet* > 	sheetsToRender;

    // determine which sheets to export, store them in sheetsToRender
    if (m_exportSpecification->allSheets) {
        foreach(Sheet* sheet, m_project->get_sheets()) {
            sheetsToRender.append(sheet);
        }
    } else {
        Sheet* sheet = qobject_cast<Sheet*>(m_project->get_current_session());
        if (sheet) {
            sheetsToRender.append(sheet);
        }
    }

    // process each sheet in the list sheetsToRender. here we set the renderpass mode,
    // and then call Sheet::repare_export() and Sheet::render(), which do the actual
    // processing.
    foreach(Sheet* sheet, sheetsToRender) {
        PMESG("Starting export for sheet %lld", sheet->get_id());
        emit m_project->exportStartedForSheet(sheet);
        m_exportSpecification->resumeTransport = false;
        m_exportSpecification->resumeTransportLocation = sheet->get_transport_location();
        // sheet->readbuffer = readbuffer;

        if (m_exportSpecification->normalize) {
            // start one render pass in mode "CALC_NORM_FACTOR"
            m_exportSpecification->m_peakValue = 0.0;
            m_exportSpecification->renderpass = TExportSpecification::CALC_NORM_FACTOR;


            if (sheet->prepare_export(m_exportSpecification) < 0) {
                PERROR("Failed to prepare sheet for export");
                continue;
            }

            sheet->start_export(m_exportSpecification);

            m_exportSpecification->normvalue = (1.0f - FLT_EPSILON) / m_exportSpecification->m_peakValue;

            if (m_exportSpecification->m_peakValue > 1.0f) {
                info().critical(tr("Detected clipping in exported audio! (%1)")
                                    .arg(coefficient_to_dbstring(m_exportSpecification->m_peakValue)));
            }

            if (!m_exportSpecification->breakout) {
                info().information(tr("calculated norm factor: %1").arg(coefficient_to_dbstring(m_exportSpecification->normvalue)));
            }
        }

        // start the real render pass in mode "WRITE_TO_HARDDISK"
        m_exportSpecification->renderpass = TExportSpecification::WRITE_TO_HARDDISK;

        // first call Sheet::prepare_export()...
        if (sheet->prepare_export(m_exportSpecification) < 0) {
            PERROR("Failed to prepare sheet for export");
            break;
        }

        // ... then start the render process and wait until it's finished
        sheet->start_export(m_exportSpecification);

        if (!QMetaObject::invokeMethod(sheet, "set_transport_pos",  Qt::QueuedConnection, Q_ARG(TTimeRef, m_exportSpecification->resumeTransportLocation))) {
            printf("Invoking Sheet::set_transport_pos() failed\n");
        }
        if (m_exportSpecification->resumeTransport) {
            if (!QMetaObject::invokeMethod(sheet, "start_transport",  Qt::QueuedConnection)) {
                printf("Invoking Sheet::start_transport() failed\n");
            }
        }
        if (m_exportSpecification->breakout) {
            break;
        }
        renderedSheets++;
    }

    PMESG("Export Finished");

    m_exportSpecification->running = false;
    overallExportProgress = 0;


    emit m_project->exportFinished();
}
