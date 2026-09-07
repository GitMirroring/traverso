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


#include "TAudioSource.h"

#include <utility>
#include "TSheet.h"
#include "TPeak.h"
#include "Utils.h"
#include "TQueueBufferSlot.h"



#include "Debugger.h"

// This constructor is called at file import or recording
TAudioSource::TAudioSource(QString  dir, const QString& name)
	: m_dir(std::move(dir))
	, m_name(name)
	, m_shortName(name)
	, m_wasRecording (false)
{
	PENTERCONS;
	m_fileName = m_dir + m_name;
    m_id = TraversoDAW::Utils::create_id();

    m_rtBufferSlotsQueue = nullptr;
    m_freeBufferSlotsQueue = nullptr;
    m_bufferstatus.set_sync_status(TAudioSourceBufferStatus::SyncStatus::OUT_OF_SYNC);

}


// This constructor is called for existing (recorded/imported) audio sources
TAudioSource::TAudioSource()
	: m_dir("")
	, m_name("")
	, m_fileName("")
	, m_wasRecording(false)
{
    m_rtBufferSlotsQueue = nullptr;
    m_freeBufferSlotsQueue = nullptr;
    m_bufferstatus.set_sync_status(TAudioSourceBufferStatus::SyncStatus::OUT_OF_SYNC);

}


TAudioSource::~TAudioSource()
{
	PENTERDES;
}

void TAudioSource::prepare_rt_buffers(nframes_t bufferSize)
{
    Q_ASSERT(m_outputRate > 0);

    uint slotcount = (m_outputRate / bufferSize);
    // Make sure we have enough slots to keep the buffer status logic intact
    if (slotcount < 5) {
        slotcount = 5;
    }

    // only re-create our buffers if slotcount and buffersize are different
    if (m_freeBufferSlotsQueue && m_rtBufferSlotsQueue && m_lastQueuedRTBufferSlot) {
        if (m_lastQueuedRTBufferSlot->get_buffer_size() == bufferSize &&
            m_slotcount == slotcount) {
            return;
        }
    }

    m_bufferstatus.set_sync_status(TAudioSourceBufferStatus::QUEUE_ABOUT_TO_BE_DELETED);

    delete_queue_buffers();

    TQueueBufferSlot* slot = nullptr;
    m_slotcount = slotcount;

    m_rtBufferSlotsQueue = new moodycamel::BlockingReaderWriterCircularBuffer<TQueueBufferSlot*>(m_slotcount);
    m_freeBufferSlotsQueue = new moodycamel::BlockingReaderWriterCircularBuffer<TQueueBufferSlot*>(m_slotcount);


    m_bufferSlotDuration = TTimeRef(bufferSize, m_outputRate);

    for (size_t i=0; i<m_slotcount;++i) {
        slot = new TQueueBufferSlot(i, m_channelCount, bufferSize);
        bool queued = m_freeBufferSlotsQueue->try_enqueue(slot);
        Q_ASSERT(queued);
    }

    Q_ASSERT(slot);
    // We have to assign m_lastQueuedRTBufferSlot to an existing slot
    m_lastQueuedRTBufferSlot = slot;

    m_bufferstatus.set_sync_status(TAudioSourceBufferStatus::SyncStatus::OUT_OF_SYNC);

    // printf("AudioSource::::prepare_rt_buffers: freeBufferSlotsQueue slot count %zu\n", m_freeBufferSlotsQueue->size_approx());
    // printf("AudioSource::::prepare_rt_buffers: rtBufferSlotsQueue slot count %zu\n", m_rtBufferSlotsQueue->size_approx());
}

void TAudioSource::delete_queue_buffers()
{
    Q_ASSERT(m_bufferstatus.get_sync_status() == TAudioSourceBufferStatus::QUEUE_ABOUT_TO_BE_DELETED);

    TQueueBufferSlot* slot;

    if (m_freeBufferSlotsQueue) {
        while(m_freeBufferSlotsQueue->try_dequeue(slot)) {
            delete slot;
            slot = nullptr;
        }
        Q_ASSERT(m_freeBufferSlotsQueue->size_approx() == 0);
        delete m_freeBufferSlotsQueue;
        m_freeBufferSlotsQueue = nullptr;
    }

    if (m_rtBufferSlotsQueue) {
        while (m_rtBufferSlotsQueue->try_dequeue(slot)) {
            delete slot;
            slot = nullptr;
        }
        Q_ASSERT(m_rtBufferSlotsQueue->size_approx() == 0);
        delete m_rtBufferSlotsQueue;
        m_rtBufferSlotsQueue = nullptr;
    }
}



void TAudioSource::set_name(const QString& name)
{
	m_name = name;
	if (m_wasRecording) {
		m_shortName = m_name.left(m_name.length() - 20);
	} else {
		m_shortName = m_name;
	}
	m_fileName = m_dir + m_name;
}


void TAudioSource::set_dir(const QString& dir)
{
	m_dir = dir;
	m_fileName = m_dir + m_name;
}


uint TAudioSource::get_sample_rate( ) const
{
	return m_rate;
}

void TAudioSource::set_original_bit_depth( uint bitDepth )
{
	m_origBitDepth = bitDepth;
}

void TAudioSource::set_created_by_sheet(qint64 id)
{
	m_origSheetId = id;
}

QString TAudioSource::get_filename( ) const
{
	return m_fileName;
}

QString TAudioSource::get_dir( ) const
{
	return m_dir;
}

QString TAudioSource::get_name( ) const
{
	return m_name;
}

uint TAudioSource::get_bit_depth( ) const
{
	return m_origBitDepth;
}

QString TAudioSource::get_short_name() const
{
	return m_shortName;
}

// eof
