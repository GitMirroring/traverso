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

#ifndef READSOURCE_H
#define READSOURCE_H

#include "AudioSource.h"
#include "TTimeRef.h"
#include "Utils.h"
#include "cameron/readerwritercircularbuffer.h"

#include <QDomDocument>


class ResampleAudioReader;
class AudioBus;
class DecodeBuffer;

struct BufferStatus {

    enum SyncStatus {
        UNKNOWN,
        OUT_OF_SYNC,
        IN_SYNC,
        QUEUE_SEEKING_TO_NEW_LOCATION,
        QUEUE_SYNCED_TO_NEW_LOCATION,
        FILL_RTBUFFER_DEQUEUE_FAILURE,
        FILL_RTBUFFER_ENQUEUE_FAILURE
    };

    inline bool out_of_sync() const {return m_syncStatus.load() != IN_SYNC;}

    inline void set_sync_status(int status) {
        m_syncStatus.store(status);
    }
    inline int get_sync_status() {
        return m_syncStatus.load();
    }

    int     fillStatus;
    int     priority;

private:
    std::atomic<int>     m_syncStatus;
};

class QueueBufferSlot {
public:
    QueueBufferSlot(int slotNumber, uint channelCount, nframes_t bufferSize) {
        m_fileLocation = TTimeRef::INVALID;
        m_slotNumber = slotNumber;
        m_bufferSize = bufferSize;
        m_channelCount = channelCount;
        for (uint i=0; i<channelCount; ++i) {
            m_buffers.append(new audio_sample_t[bufferSize]);
        }
    }
    ~QueueBufferSlot() {
        for (uint i=0; i<m_channelCount; ++i) {
            delete [] m_buffers.at(i);
        }
    }

    int get_slot_number() const {return m_slotNumber;}
    inline nframes_t get_buffer_size() const {return m_bufferSize;}
    inline TTimeRef get_file_location() const {return m_fileLocation;}

    audio_sample_t* get_buffer(uint channel) {
        Q_ASSERT(channel < m_channelCount);
        return m_buffers.at(channel);
    }

    void read_buffer(audio_sample_t* dest, uint channel, nframes_t nframes) {
        Q_ASSERT(nframes <= m_bufferSize);
        Q_ASSERT(channel < m_channelCount);
        Q_ASSERT(nframes > 0);
        memcpy(dest, m_buffers.at(channel), nframes * sizeof(audio_sample_t));
    }

    void write_buffer(const TTimeRef &fileLocation, audio_sample_t* source, uint channel, nframes_t nframes) {
        Q_ASSERT(nframes == m_bufferSize);
        Q_ASSERT(nframes > 0);
        memcpy(m_buffers.at(channel), source, nframes * sizeof(audio_sample_t));
        m_fileLocation = fileLocation;
    }

    void set_file_location(const TTimeRef& fileLocation) {
        m_fileLocation = fileLocation;
    }

    void print_state() const {
        printf("TransportLocation %s, slotnumber %d\n", QS_C(TTimeRef::timeref_to_ms_3(m_fileLocation)), m_slotNumber);
    }

private:
    TTimeRef            m_fileLocation;
    int                 m_slotNumber;
    nframes_t           m_bufferSize;
    uint                m_channelCount;
    QList<audio_sample_t*>   m_buffers;
};

class ReadSource : public AudioSource
{
	Q_OBJECT
public :
	ReadSource(const QDomNode &node);
	ReadSource(const QString& dir, const QString& name);
    ReadSource(const QString& dir, const QString& name, uint channelCount);
	ReadSource();  // For creating a 0-channel, silent ReadSource
	~ReadSource();
	
	enum ReadSourceError {
        COULD_NOT_OPEN_FILE = -1,
        INVALID_CHANNEL_COUNT = -2,
        ZERO_CHANNELS = -3,
        FILE_DOES_NOT_EXIST = -4
    };
	
	ReadSource* deep_copy();
	
	int set_state( const QDomNode& node );
	QDomNode get_state(QDomDocument doc);

    nframes_t ringbuffer_read(AudioBus *audioBus, const TTimeRef &fileLocation, nframes_t frames);

    void rb_seek_to_transport_location(const TTimeRef &transportLocation);

    int file_read(DecodeBuffer* buffer, const TTimeRef& fileLocation, nframes_t cnt) const;
    int file_read(DecodeBuffer* buffer, nframes_t fileLocation, nframes_t cnt);

	int init();
	int get_error() const {return m_error;}
	QString get_error_string() const;
	int set_file(const QString& filename);
	void set_active(bool active);
	
	nframes_t get_nframes() const;
    uint get_file_rate() const;
    uint get_output_rate() const {return m_outputRate;}
	const TTimeRef& get_length() const {return m_length;}

    void fill_realtime_buffers();
    void prepare_rt_buffers(nframes_t bufferSize);

    BufferStatus* get_buffer_status();
	
    void set_output_rate_and_convertor_type(int outputRate, int converterType);
    void set_decode_buffers(DecodeBuffer * fileReadBuffer, DecodeBuffer *resampleDecodeBuffer);

    void set_transport_start_location(const TTimeRef &transportStartLocation);
    void set_source_start_location(const TTimeRef &sourceStartLocation);
	
	
private:
    ResampleAudioReader*	m_resampleAudioReader;

    DecodeBuffer*       m_fileDecodeBuffer;
    int                 m_refcount;
    int                 m_error;
    bool                m_silent;
    std::atomic<bool>   m_active;
	
    TTimeRef            m_length;
    TTimeRef            m_transportStartLocation;
    TTimeRef            m_sourceStartLocation;
    uint                m_outputRate;

    moodycamel::BlockingReaderWriterCircularBuffer<QueueBufferSlot*> *m_rtBufferSlotsQueue;
    moodycamel::BlockingReaderWriterCircularBuffer<QueueBufferSlot*> *m_freeBufferSlotsQueue;
    QueueBufferSlot*    m_lastQueuedRTBufferSlot;
    BufferStatus		m_bufferstatus;
    TTimeRef            m_bufferSlotDuration;
	
	int ref() { return m_refcount++;}
	
	void private_init();
    void delete_readbuffer_queues();

	friend class ResourcesManager;
	friend class ProjectConverter;

signals:
	void stateChanged();
};

#endif
