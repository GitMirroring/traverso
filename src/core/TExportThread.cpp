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
#include "DiskIO.h"
#include "Sheet.h"
#include "TExportSpecification.h"
#include "Project.h"

// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"
#include "Utils.h"

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

    for (auto sheet : m_exportSpecification->get_sheets_to_export()) {

        emit m_exportSpecification->exportMessage(QString("Starting export of %1").arg(sheet->get_name()));
        m_exportSpecification->resumeTransportLocation = sheet->get_transport_location();
        m_exportSpecification->set_export_file_name("Sheet_" + QString::number(m_project->get_sheet_index(sheet->get_id())) +"-" + sheet->get_name());

        TTimeRef exportStartLocation, exportEndLocation;
        bool exportRangeAvailable = false;
        if (m_exportSpecification->is_cd_export()) {
            exportRangeAvailable = sheet->get_cd_export_range(exportStartLocation, exportEndLocation);
        } else {
            exportRangeAvailable = sheet->get_export_range(exportStartLocation, exportEndLocation);
        }

        if (!exportRangeAvailable) {
            printf("No export range available for Sheet %s\n", QS_C(sheet->get_name()));
            return;
        }

        m_exportSpecification->set_export_start_location(exportStartLocation);
        m_exportSpecification->set_export_end_location(exportEndLocation);

        // ... then start the render process and wait until it's finished
        sheet->set_transport_location(m_exportSpecification->get_export_start_location());
        sheet->start_transport();
        usleep(500 * 1000);

        emit m_exportSpecification->exportMessage(QString("Starting export of %1").arg(sheet->get_name()));
        m_exportSpecification->print_export_data();

        do {
            nframes_t diff = m_exportSpecification->get_remaining_export_frames();
            nframes_t nframes = std::min(diff, m_exportSpecification->get_block_size());

            sheet->process(nframes);
            m_exportSpecification->add_exported_range(TTimeRef(nframes, audiodevice().get_sample_rate()));
        } while(!m_exportSpecification->cancel_export_requested() && m_exportSpecification->get_remaining_export_frames() > 0);


        emit m_exportSpecification->exportMessage(QString("Finished export of %1").arg(sheet->get_name()));


        if (!QMetaObject::invokeMethod(sheet, "set_transport_location",  Qt::QueuedConnection, Q_ARG(TTimeRef, m_exportSpecification->resumeTransportLocation))) {
            printf("Invoking Sheet::set_transport_pos() failed\n");
        }
        if (m_exportSpecification->resumeTransport) {
            if (!QMetaObject::invokeMethod(sheet, "start_transport",  Qt::QueuedConnection)) {
                printf("Invoking Sheet::start_transport() failed\n");
            }
        }

    }

    PMESG("Export Finished");


    emit m_project->exportFinished();
}
