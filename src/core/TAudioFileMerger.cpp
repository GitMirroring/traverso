/*
Copyright (C) 2007 Remon Sijrier 

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

#include "TAudioFileMerger.h"
#include <QFile>
#include <QMutexLocker>

#include "TExportSpecification.h"
#include "TFileDecodeBuffer.h"
#include "TReadAudioSource.h"
#include "TWriteAudioSource.h"
#include "TPeak.h"
#include "defines.h"

#include "Debugger.h"

TAudioFileMerger::TAudioFileMerger()
{
	m_stopMerging = false;
	moveToThread(this);
	start();
	connect(this, &TAudioFileMerger::dequeueTask, this, &TAudioFileMerger::dequeue_tasks, Qt::QueuedConnection);
}

void TAudioFileMerger::enqueue_task(TReadAudioSource * source0, TReadAudioSource * source1, const QString& dir, const QString & outfilename)
{
	MergeTask task;
	task.readsource0 = source0;
	task.readsource1 = source1;
	task.outFileName = outfilename;
	task.dir = dir;
	
	m_mutex.lock();
	m_tasks.enqueue(task);
	m_mutex.unlock();
	
	emit dequeueTask();
}

void TAudioFileMerger::dequeue_tasks()
{
	m_mutex.lock();
	if (m_tasks.size()) {
		MergeTask task = m_tasks.dequeue();
		m_mutex.unlock();
		process_task(task);
		return;
	}
	m_mutex.unlock();
}

// FIXME
// (MUCH) cody copy from void AudioFileCopyConvert::process_task(CopyTask task) ??
void TAudioFileMerger::process_task(MergeTask task)
{
	QString name = task.readsource0->get_name();
	int length = name.length();

    emit taskStarted(name.left(length-28));

    TFileDecodeBuffer decodebuffer0;
	TFileDecodeBuffer decodebuffer1;
	
    TExportSpecification spec;
    spec.set_export_start_location(TTimeRef());
    spec.set_export_end_location(task.readsource0->get_length());

    spec.set_export_dir(task.dir);
    spec.extraFormat["filetype"] = "wav";
    spec.set_channel_count(2);
    spec.set_sample_rate(task.readsource0->get_sample_rate());
    spec.set_export_file_name(task.outFileName);
	
    TWriteAudioSource writesource(&spec);
    if (writesource.prepare_export() == -1) {
		return;
	}

	// Enable on the fly generation of peak data to speedup conversion 
	// (no need to re-read all the audio files to generate peaks)
    writesource.set_process_peaks(true);
	
	do {
		// if the user asked to stop processing, jump out of this 
		// loop, and cleanup any resources in use.
		if (m_stopMerging) {
            PMESG("AudioFileMerger::process_task: Stop Merging was requested, breaking out of process loop");
            break;
		}

        nframes_t diff = spec.get_remaining_export_frames();
        nframes_t this_nframes = std::min(diff, spec.get_block_size());
		nframes_t nframes = this_nframes;

        spec.silence_render_buffer(nframes);
		
        task.readsource0->file_read(&decodebuffer0, spec.get_export_location(), nframes);
        task.readsource1->file_read(&decodebuffer1, spec.get_export_location(), nframes);

        audio_sample_t* destinationLeft = decodebuffer0.get_destination_buffer(0).get_data(nframes);
        audio_sample_t* destinationRight = decodebuffer1.get_destination_buffer(0).get_data(nframes);
        for (uint x = 0; x < nframes; ++x) {
            spec.get_render_buffer()[x*spec.get_channel_count()] = destinationLeft[x];
            spec.get_render_buffer()[1+(x*spec.get_channel_count())] = destinationRight[x];
		}
		
		// due the fact peak generating does _not_ happen in writesource->process
		// but in a function used by DiskIO, we have to hack the peak processing 
		// in here.
        writesource.get_peak()->process(0, destinationLeft, nframes);
        writesource.get_peak()->process(1, destinationRight, nframes);
		
		// Process the data, and write to disk
        // FIXME
        // Shouldn't we use the actual read frames count instead of block size?
        // The end of the file will be most likely not a multiple of block size.
        writesource.process(spec.get_block_size());
		
        spec.add_exported_range(TTimeRef(nframes, task.readsource0->get_sample_rate()));

    } while (spec.get_remaining_export_frames() > 0);


    if (m_stopMerging) {
        PMESG("AudioFileMerger::process_task: Stop Merging was requested, WriterSource finish export called");
        writesource.finish_export();
    }
	
	//  The user asked to stop processing, exit the event loop
	// and signal we're done.
	if (m_stopMerging) {
		exit(0);
		wait(1000);
		m_tasks.clear();
		emit processingStopped();
		return;
	}
	
	emit taskFinished(name.left(length-28));
}

void TAudioFileMerger::stop_merging()
{
	m_stopMerging = true;
}

