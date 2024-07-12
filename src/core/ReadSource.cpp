/*
Copyright (C) 2006-2007 Remon Sijrier 

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

#include "ReadSource.h"
#include "ResampleAudioReader.h"

#include "ProjectManager.h"
#include "Project.h"
#include "AudioClip.h"
#include "DiskIO.h"
#include "Utils.h"
#include "AudioDevice.h"
#include <QFile>
#include "TConfig.h"

// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"


/**
 *	\class ReadSource
	\brief A class for (buffered) reading of audio files.
 */

size_t slotcount = 30;


// #define PRINT_BUFFER_STATUS

// This constructor is called for existing (recorded/imported) audio sources
ReadSource::ReadSource(const QDomNode& node)
	: AudioSource()
{
	
	set_state(node);
	
	private_init();
	
	Project* project = pm().get_project();
	
	// FIXME The check below no longer makes sense!!!!!
	// Check if the audiofile exists in our project audiosources dir
	// and give it priority over the dir as given by the project.tpf file
	// This makes it possible to move project directories without Traverso being
	// unable to find it's audiosources!
	if ( QFile::exists(project->get_root_dir() + "/audiosources/" + m_name) || 
	     QFile::exists(project->get_root_dir() + "/audiosources/" + m_name + "-ch0.wav") ) {
		set_dir(project->get_root_dir() + "/audiosources/");
	}
	
	m_silent = (m_channelCount == 0);
}	

// constructor for file import
ReadSource::ReadSource(const QString& dir, const QString& name)
	: AudioSource(dir, name)
{
	private_init();
	
	AbstractAudioReader* reader = AbstractAudioReader::create_audio_reader(m_fileName);

	if (reader) {
		m_channelCount = reader->get_num_channels();
		delete reader;
	} else {
		m_channelCount = 0;
	}

	m_silent = false;
}


// Constructor for recorded audio.
ReadSource::ReadSource(const QString& dir, const QString& name, uint channelCount)
	: AudioSource(dir, name)
{
	private_init();
	
	m_channelCount = channelCount;
	m_silent = false;
	m_name = name  + "-" + QString::number(m_id);
	m_fileName = m_dir + m_name;
	m_rate = pm().get_project()->get_rate();
	m_wasRecording = true;
	m_shortName = m_name.left(m_name.length() - 20);
}


// Constructor for silent clips
ReadSource::ReadSource()
	: AudioSource("", tr("Silence"))
{
	private_init();
	
	m_channelCount = 0;
	m_silent = true;
}


void ReadSource::private_init()
{
	m_refcount = 0;
	m_error = 0;
    m_clip = nullptr;
    m_resampleAudioReader = nullptr;
    m_rtBufferSlotsQueue = nullptr;
    m_freeBufferSlotsQueue = nullptr;
    m_bufferstatus.syncStatus = BufferStatus::SyncStatus::OUT_OF_SYNC;

}


ReadSource::~ReadSource()
{
	PENTERDES;
	for(int i=0; i<m_buffers.size(); ++i) {
		delete m_buffers.at(i);
	}
	
    if (m_resampleAudioReader) {
        delete m_resampleAudioReader;
    }
}

QDomNode ReadSource::get_state( QDomDocument doc )
{
	QDomElement node = doc.createElement("Source");
	node.setAttribute("channelcount", m_channelCount);
	node.setAttribute("origsheetid", m_origSheetId);
	node.setAttribute("dir", m_dir);
	node.setAttribute("id", m_id);
        node.setAttribute("name", m_name);
	node.setAttribute("origbitdepth", m_origBitDepth);
	node.setAttribute("wasrecording", m_wasRecording);
	node.setAttribute("length", m_length.universal_frame());
	node.setAttribute("rate", m_rate);

	return node;
}


int ReadSource::set_state( const QDomNode & node )
{
	PENTER;
	
	QDomElement e = node.toElement();
    m_channelCount = e.attribute("channelcount", "0").toUInt();
	m_origSheetId = e.attribute("origsheetid", "0").toLongLong();
	set_dir( e.attribute("dir", "" ));
	m_id = e.attribute("id", "").toLongLong();
	m_rate = m_outputRate = e.attribute("rate", "0").toUInt();
	bool ok;
	m_length = TTimeRef(e.attribute("length", "0").toLongLong(&ok));
    m_origBitDepth = e.attribute("origbitdepth", "0").toUInt();
	m_wasRecording = e.attribute("wasrecording", "0").toInt();
	
	// For older project files, this should properly detect if the 
	// audio source was a recording or not., in fact this should suffice
	// and the flag wasrecording would be unneeded, but oh well....
	if (m_origSheetId != 0) {
		m_wasRecording = true;
	}
	
	set_name( e.attribute("name", "No name supplied?" ));
	
	return 1;
}


int ReadSource::init( )
{
	PENTER;
	
	Q_ASSERT(m_refcount);
	
	Project* project = pm().get_project();
	
    m_fileDecodeBuffer = nullptr;
    m_active.store(false);

	// Fake the samplerate, until it's set by an AudioReader!
	if (project) {
		m_rate = m_outputRate = project->get_rate();
	} else {
		m_rate = 44100;
	}
	
	if (m_silent) {
        m_length = TTimeRef::max_length();
		m_channelCount = 0;
		m_origBitDepth = 16;
        m_bufferstatus.fillStatus =  100;
        m_bufferstatus.syncStatus = BufferStatus::SyncStatus::IN_SYNC;
		return 1;
	}
	
	if (m_channelCount == 0) {
		PERROR("ReadSource channel count is 0");
		return (m_error = INVALID_CHANNEL_COUNT);
	}
	
	if ( ! QFile::exists(m_fileName)) {
		return (m_error = FILE_DOES_NOT_EXIST);
	}

	// There should be another config option for ConverterType to use for export (higher quality)
	//converter_type = config().get_property("Conversion", "ExportResamplingConverterType", 0).toInt();
    m_resampleAudioReader = new ResampleAudioReader(m_fileName);
	
    if (!m_resampleAudioReader->is_valid()) {
//		PERROR("ReadSource:: audio reader is not valid! (reader channel count: %d, nframes: %d", m_audioReader->get_num_channels(), m_audioReader->get_nframes());
        delete m_resampleAudioReader;
        m_resampleAudioReader = nullptr;
		return (m_error = COULD_NOT_OPEN_FILE);
	}
	
    int converter_type = config().get_property("Conversion", "RTResamplingConverterType", ResampleAudioReader::get_default_resample_quality()).toInt();
    m_resampleAudioReader->set_converter_type(converter_type);
	
    set_output_rate(m_resampleAudioReader->get_file_rate());
	
    m_channelCount = m_resampleAudioReader->get_num_channels();
	
	// @Ben: I thought we support any channel count now ??
       // if (m_channelCount > 2) {
       //  PERROR(QString("ReadAudioSource: file contains %1 channels; only 2 channels are supported").arg(m_channelCount));
       //         delete m_resampleAudioReader;
       //         m_resampleAudioReader = 0;
       //         return (m_error = INVALID_CHANNEL_COUNT);
       // }

	// Never reached, it's allready checked in AbstractAudioReader::is_valid() which was allready called!
	if (m_channelCount == 0) {
//		PERROR("ReadAudioSource: not a valid channel count: %d", m_channelCount);
        delete m_resampleAudioReader;
        m_resampleAudioReader = nullptr;
		return (m_error = ZERO_CHANNELS);
	}
	
    m_rate = m_resampleAudioReader->get_file_rate();
    m_length = m_resampleAudioReader->get_length();
	
	return 1;
}


void ReadSource::set_output_rate(int rate)
{
	Q_ASSERT(rate > 0);
	
    if (! m_resampleAudioReader) {
		printf("ReadSource::set_output_rate: No audioreader!\n");
		return;
	}
	
	bool useResampling = config().get_property("Conversion", "DynamicResampling", true).toBool();
	if (useResampling) {
        m_resampleAudioReader->set_output_rate(rate);
	} else {
        m_resampleAudioReader->set_output_rate(m_resampleAudioReader->get_file_rate());
	}

    m_outputRate = rate;
	
	// The length could have become slightly smaller/larger due
	// rounding issues involved with converting to one samplerate to another.
	// Should be at the order of one - two samples at most, but for reading purposes we 
	// need sample accurate information!
    m_length = m_resampleAudioReader->get_length();
}


int ReadSource::file_read(DecodeBuffer* buffer, const TTimeRef& fileLocation, nframes_t cnt) const
{
    Q_ASSERT(m_resampleAudioReader);
    return m_resampleAudioReader->read_from(buffer, fileLocation, cnt);
}


int ReadSource::file_read(DecodeBuffer * buffer, nframes_t fileLocation, nframes_t cnt)
{
    Q_ASSERT(m_resampleAudioReader);
    return m_resampleAudioReader->read_from(buffer, fileLocation, cnt);
}


ReadSource * ReadSource::deep_copy( )
{
	PENTER;
	
	QDomDocument doc("ReadSource");
	QDomNode rsnode = get_state(doc);
	ReadSource* source = new ReadSource(rsnode);
	return source;
}

void ReadSource::set_audio_clip(AudioClip* clip)
{
	PENTER;
	Q_ASSERT(clip);
	m_clip = clip;
}

nframes_t ReadSource::get_nframes( ) const
{
    if (!m_resampleAudioReader) {
		return 0;
	}
    return m_resampleAudioReader->get_nframes();
}

int ReadSource::set_file(const QString & filename)
{
	PENTER;
	
	Q_ASSERT(m_clip);

	m_error = 0;
	
	int splitpoint = filename.lastIndexOf("/") + 1;
	int length = filename.length();
	
	QString dir = filename.left(splitpoint - 1) + "/";
	QString name = filename.right(length - splitpoint);
		
	set_dir(dir);
	set_name(name);
	
	if (init() < 0) {
		return -1;
	}
	
	set_audio_clip(m_clip);
	
	emit stateChanged();
	
	return 1;
}


void ReadSource::prepare_rt_buffers(DecodeBuffer* fileDecodeBuffer, const TTimeRef &transportLocation)
{
    printf("prepare_rt_buffers2: audio device buffer size %d\n", audiodevice().get_buffer_size());

    m_fileDecodeBuffer = fileDecodeBuffer;

    QueueBufferSlot* slot;

    if (m_freeBufferSlotsQueue) {
        while(m_freeBufferSlotsQueue->try_dequeue(slot)) {
            delete slot;
        }
        Q_ASSERT(m_freeBufferSlotsQueue->size_approx() == 0);
        delete m_freeBufferSlotsQueue;
    }

    if (m_rtBufferSlotsQueue) {
        while (m_rtBufferSlotsQueue->try_dequeue(slot)) {
            delete slot;
        }
        Q_ASSERT(m_rtBufferSlotsQueue->size_approx() == 0);
        delete m_rtBufferSlotsQueue;
    }

    m_rtBufferSlotsQueue = new moodycamel::BlockingReaderWriterCircularBuffer<QueueBufferSlot*>(slotcount);
    m_freeBufferSlotsQueue = new moodycamel::BlockingReaderWriterCircularBuffer<QueueBufferSlot*>(slotcount);


    uint bufferSize = audiodevice().get_buffer_size();
    m_bufferSlotDuration = TTimeRef(bufferSize, m_outputRate);

    for (size_t i=0; i<slotcount;++i) {
        slot = new QueueBufferSlot(i, m_channelCount, bufferSize);
        bool queued = m_freeBufferSlotsQueue->try_enqueue(slot);
        if (i==0) {
            // We have to assign m_lastQueuedRTBufferSlot to an existing slot
            m_lastQueuedRTBufferSlot = slot;
        }
        Q_ASSERT(queued);
    }

    rb_seek_to_transport_location(transportLocation);

    printf("ReadSource::prepare_rt_buffers2: rtUsedSlotsQueue slot count %zu\n", m_freeBufferSlotsQueue->size_approx());
    printf("ReadSource::prepare_rt_buffers2: rtQueue slot count %zu\n", m_rtBufferSlotsQueue->size_approx());
}


void ReadSource::rb_seek_to_transport_location(const TTimeRef& transportLocation)
{
    Q_ASSERT(m_clip);

    printf("rb_seek_to_transport_location: seeking to %s\n", QS_C(TTimeRef::timeref_to_ms_3(transportLocation)));

    QueueBufferSlot* slot;
    // The contents of the Slots in the RT queue are most likely useless due to seeking
    // to another transport location.
    // NB: Since we are seeking we are allowed and should clear the rt queue now
    while (m_rtBufferSlotsQueue->try_dequeue(slot)) {
        m_freeBufferSlotsQueue->try_enqueue(slot);
    }

    Q_ASSERT(m_rtBufferSlotsQueue->size_approx() == 0);
    Q_ASSERT(m_freeBufferSlotsQueue->size_approx() == slotcount);

    TTimeRef fileLocation = transportLocation - m_clip->get_location_start() - m_clip->get_source_start_location();

    // check if the clip's start position is within the range
    // if not, fill the buffer from the earliest point this clip
    // will come into play.
    if (fileLocation < TTimeRef()) {
        printf("not seeking to file location %s, but to file location %s\n",
               QS_C(TTimeRef::timeref_to_ms_3(fileLocation)), QS_C(TTimeRef::timeref_to_ms_3(m_clip->get_source_start_location())));
        fileLocation = m_clip->get_source_start_location();
    }

    TTimeRef seekTransportLocation = transportLocation;
    if (seekTransportLocation < m_clip->get_location_start()) {
        seekTransportLocation = m_clip->get_location_start();
        printf("transport location before clip start position, adjusting to clip start position %s\n",
               QS_C(TTimeRef::timeref_to_ms_3(seekTransportLocation)));
    }


    m_lastQueuedRTBufferSlot->set_locations(seekTransportLocation, fileLocation);
}

void ReadSource::fill_realtime_buffers(bool seeking)
{
    Q_ASSERT(m_lastQueuedRTBufferSlot);
    Q_ASSERT(m_fileDecodeBuffer);

    // printf("ReadSource::process_ringbuffer2\n");
    if (m_channelCount == 0) {
        return;
    }

    // Check if the resample quality has changed, it's a safe place here
    // to reconfigure the audioreaders resample quality.
    // This allows on the fly changing of the resample quality :)
    if (m_diskio->get_resample_quality() != m_resampleAudioReader->get_convertor_type()) {
        m_resampleAudioReader->set_converter_type(m_diskio->get_resample_quality());
    }

    auto freeSlots = m_freeBufferSlotsQueue->size_approx();
    if (freeSlots == 0) {
        printf("Free Buffer Slots Queue is empty, why was I called?\n");
        return;
    }

    QueueBufferSlot* slot = nullptr;
    TTimeRef slotTransportLocation = m_lastQueuedRTBufferSlot->get_transport_location();
    TTimeRef slotFileLocation = m_lastQueuedRTBufferSlot->get_file_location();
    auto bufferSize = m_lastQueuedRTBufferSlot->get_buffer_size();
    // int filledSlots = 0;

    // We need the next slot so add buffer size length to the last slot transport location
    // except when we are seeking, then the rt queueu actually is empty and we need to
    // read to the m_lastQueuedRTBufferSlot->get_transport_location(); since we set that
    // value to the seek transport location
    size_t slotsToFill = freeSlots - 1;  // leave one slot in the rt queue so the ringbuffer_read() Queue Buffer Slot cannot be overwritten by us
    if (seeking) {
        slotsToFill = int(0.6 * slotcount);
    } else {
        slotTransportLocation += m_bufferSlotDuration;
        slotFileLocation += m_bufferSlotDuration;
    }

    while (slotsToFill)
    {
        nframes_t read = file_read(m_fileDecodeBuffer, slotFileLocation, bufferSize);

        if (read != bufferSize) { // likely end of file
            // printf("ReadSource::fill_realtime_buffers: file_read gave only %d\n", read);
        }


        if (!m_freeBufferSlotsQueue->try_dequeue(slot)) {
            PERROR("ReadSource::fill_realtime_buffers: try dequeue failed");
            m_bufferstatus.syncStatus = BufferStatus::FILL_RTBUFFER_DEQUEUE_FAILURE;
            return;
        }

        if (read > 0) {
            for (uint chan=0; chan<m_channelCount; ++chan) {
                slot->write_buffer(slotTransportLocation, slotFileLocation, m_fileDecodeBuffer->destination[chan], chan, bufferSize);
            }
        }

        if (!m_rtBufferSlotsQueue->try_enqueue(slot)) {
            PERROR("ReadSource::fill_realtime_buffers: try enqueue failed");
            m_bufferstatus.syncStatus = BufferStatus::FILL_RTBUFFER_ENQUEUE_FAILURE;
            return;
        }

        // slot->print_state();

        slotTransportLocation += m_bufferSlotDuration;
        slotFileLocation += m_bufferSlotDuration;

        slotsToFill--;
    }

    m_lastQueuedRTBufferSlot = slot;
    Q_ASSERT(m_lastQueuedRTBufferSlot);

    m_bufferstatus.syncStatus = BufferStatus::SyncStatus::IN_SYNC;
}


nframes_t ReadSource::ringbuffer_read(audio_sample_t **dest, const TTimeRef &startLocation, nframes_t cnt)
{
    if (! (m_bufferstatus.syncStatus == BufferStatus::SyncStatus::IN_SYNC)) {
        return 0;
    }

    QueueBufferSlot* slot = nullptr;

    // auto startTime = TTimeRef::get_nanoseconds_since_epoch();
    nframes_t read = 0;
    auto availableSlots = m_rtBufferSlotsQueue->size_approx();


    while (m_rtBufferSlotsQueue->try_dequeue(slot))
    {
        Q_ASSERT(slot);

        m_freeBufferSlotsQueue->try_enqueue(slot); // always put the dequeued slot on the free slots queue so we don't lose slots

        // check if this slot or any available is a candidate slot, if not, no need to process the
        // whole queue, instead start a resync


        TTimeRef slotTransportLocation = slot->get_transport_location();
        Q_ASSERT(slotTransportLocation != TTimeRef::INVALID);

        if (slotTransportLocation == startLocation)
        {
            for (uint chan=0; chan < m_channelCount; ++chan) {
                slot->read_buffer(dest[chan], chan, cnt);
            }

            // slot->print_state();
            read = slot->get_buffer_size();
            break;
        }

        TTimeRef lastAvailableSlotTransportLocation = slotTransportLocation + availableSlots * m_bufferSlotDuration;

        // Check transport location in queue range
        if ((startLocation < slotTransportLocation) || (startLocation > lastAvailableSlotTransportLocation)) {
            printf("ReadSource::ringbuffer_read: TransportLocation not in queue range: %s (%s - %s)\n",
                   QS_C(TTimeRef::timeref_to_ms_3(startLocation)),
                   QS_C(TTimeRef::timeref_to_ms_3(slotTransportLocation)),
                   QS_C(TTimeRef::timeref_to_ms_3(lastAvailableSlotTransportLocation)));
            m_bufferstatus.syncStatus = BufferStatus::SyncStatus::OUT_OF_SYNC;
            read = 0;
            break;
        }

        printf("ReadSource::rb_read: Skipping slot %d, location %s\n",
               slot->get_slot_number(), QS_C(TTimeRef::timeref_to_ms_3(slotTransportLocation)));
    }

    // auto totalTime = TTimeRef::get_nanoseconds_since_epoch() - startTime;
    // if (totalTime > 20) {
        // printf("ReadSource::rb_read2: took nanosecs: %ld\n", totalTime);
    // }

    return read;
}

BufferStatus* ReadSource::get_buffer_status()
{
    if (!m_active.load() || (m_channelCount == 0)) {
        m_bufferstatus.fillStatus =  100;
	} else {
        m_bufferstatus.fillStatus = 100 - ((m_freeBufferSlotsQueue->size_approx() * 100) / slotcount);
	}

    return &m_bufferstatus;
}

void ReadSource::set_active(bool active)
{
    m_active.store(active);
}

uint ReadSource::get_file_rate() const
{
    if (m_resampleAudioReader) {
        return m_resampleAudioReader->get_file_rate();
	} else {
		PERROR("ReadSource::get_file_rate(), but no audioreader available!!");
	}
	
	return pm().get_project()->get_rate(); 
}

void ReadSource::set_diskio(DiskIO * diskio)
{
	m_diskio = diskio;
	set_output_rate(m_diskio->get_output_rate());
	
    if (m_resampleAudioReader) {
        m_resampleAudioReader->set_resample_decode_buffer(m_diskio->get_resample_decode_buffer());
        m_resampleAudioReader->set_converter_type(m_diskio->get_resample_quality());
    }
}

QString ReadSource::get_error_string() const
{
	switch(m_error) {
		case COULD_NOT_OPEN_FILE: return tr("Could not open file");
		case INVALID_CHANNEL_COUNT: return tr("Invalid channel count");
		case ZERO_CHANNELS: return tr("File has zero channels");
		case FILE_DOES_NOT_EXIST: return tr("The file does not exist!");
	}
	return tr("No ReadSource error set");
}

