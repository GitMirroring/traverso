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

#ifndef T_AUDIO_SOURCE_H
#define T_AUDIO_SOURCE_H

#include "TAudioSourceBufferStatus.h"
#include "TTimeRef.h"
#include "defines.h"

#include "cameron/readerwritercircularbuffer.h"

#include <QObject>

class TFileDecodeBuffer;
class TQueueBufferSlot;

/// The base class for AudioSources like ReadSource and WriteSource
class AudioSource : public QObject
{
    Q_OBJECT

public :
	AudioSource();
	AudioSource(QString  dir, const QString& name);
        virtual ~AudioSource();
	
	void set_name(const QString& name);
	void set_dir(const QString& name);
	void set_original_bit_depth(uint bitDepth);
	void set_created_by_sheet(qint64 id);
	QString get_filename() const;
	QString get_dir() const;
	QString get_name() const;
	QString get_short_name() const;
        qint64 get_id() const {return m_id;}
	qint64 get_orig_sheet_id() const {return m_origSheetId;}
    uint get_sample_rate() const;
        uint get_channel_count() const {return m_channelCount;}
    uint get_bit_depth() const;

    virtual TAudioSourceBufferStatus* get_buffer_status() = 0;
    uint get_output_rate() const {return m_outputRate;}

	
protected:
    TAudioSourceBufferStatus		m_bufferstatus;

    moodycamel::BlockingReaderWriterCircularBuffer<TQueueBufferSlot*> *m_rtBufferSlotsQueue;
    moodycamel::BlockingReaderWriterCircularBuffer<TQueueBufferSlot*> *m_freeBufferSlotsQueue;
    TQueueBufferSlot*    m_lastQueuedRTBufferSlot;

    TTimeRef            m_bufferSlotDuration;
    uint                m_outputRate;
    size_t              m_slotcount;

    // Used for WriteSource, change to DecodeBuffer
    audio_sample_t* m_diskIOFramebuffer;

    uint		m_channelCount;
    qint64		m_origSheetId{};
	QString 	m_dir;
	qint64		m_id{};
	QString 	m_name;
	QString		m_shortName;
    uint		m_origBitDepth{};
	QString		m_fileName;
    // FIMXE : use output rate instead ?
    uint 		m_rate{};
	int		m_wasRecording;

private:
    // Creation and deletion of the RT buffers can only happen if we know
    // these buffers are not accessed by the audio thread.
    // DiskIO is used to synchronize communication between audio thread and the
    // other threads so let DiskIO prepare and delete the buffers since it 'knows'
    // when it is save to do so
    friend class DiskIO;
    void prepare_rt_buffers(nframes_t bufferSize);
    void delete_queue_buffers();
    virtual void process_realtime_buffers() = 0;
    virtual void rb_seek_to_transport_location(const TTimeRef &transportLocation) = 0;
    virtual void set_output_rate_and_convertor_type(int outputRate, int converterType) = 0;
    virtual void set_decode_buffers(TFileDecodeBuffer * fileReadBuffer, TFileDecodeBuffer *resampleDecodeBuffer) = 0;
    // Used in WriteSource, change to use DecodeBuffers instead
    void set_diskio_frame_buffer(audio_sample_t* frameBuffer) {
        m_diskIOFramebuffer = frameBuffer;
    }


};


#endif
