/*
Copyright (C) 2006-2024 Remon Sijrier

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

#ifndef T_DISKIO_THREAD_H
#define T_DISKIO_THREAD_H

#include <QList>
#include <QThread>

#include "TFileDecodeBuffer.h"
#include "TTimeRef.h"
#include "defines.h"

#include "cameron/readerwritercircularbuffer.h"

class TAudioSource;

class TDiskIOThread : public QThread
{
	Q_OBJECT

public:
    TDiskIOThread();
    ~TDiskIOThread();
	
	static const int writebuffertime = 5;

    void set_transport_location(const TTimeRef& transportLocation) {
        m_transportLocation = transportLocation;
    }

    void set_seek_transport_location(const TTimeRef& transportLocation) {
        m_seekTransportLocation = transportLocation;
        m_seekRequested.store(true);
    }

    void add_audio_source(TAudioSource* source);
    void remove_audio_source(TAudioSource* source);

    bool get_cpu_time(float &time);
    int get_buffers_fill_status();
    uint get_output_rate() {return m_outputSampleRate;}
	int get_resample_quality() {return m_resampleQuality;}

    void add_processed_audio_thread_frames(nframes_t nframes)
    {
        m_audioThreadProcessedFramesQueue->try_enqueue(nframes);
    }
    void wakeup()
    {
        m_audioThreadProcessedFramesQueue->try_enqueue(0);
    }

protected:
    void run() override;

private:
    std::unique_ptr<moodycamel::BlockingReaderWriterCircularBuffer<nframes_t>>      m_audioThreadProcessedFramesQueue;
    std::unique_ptr<moodycamel::BlockingReaderWriterCircularBuffer<TAudioSource*>>   m_audioSourcesToBeAdded;
    std::unique_ptr<moodycamel::BlockingReaderWriterCircularBuffer<TAudioSource*>>   m_audioSourcesToBeRemoved;

    std::atomic<bool>   m_seekRequested;
    bool                m_stopDiskIOThreadRequested;

    QList<TAudioSource*>	m_audioSources;

    std::atomic<int>    m_bufferFillStatus;
    std::atomic<trav_time_t> m_doWorktTime;
    trav_time_t         m_lastCpuReadTime;

    int                 m_resampleQuality;
    bool                m_resampleQualityChanged;
    bool                m_sampleRateChanged;

    std::shared_ptr<TFileDecodeBuffer>		m_fileDecodeBuffer;
    std::shared_ptr<TFileDecodeBuffer>		m_resampleDecodeBuffer;
    uint                m_outputSampleRate{};

    TTimeRef            m_transportLocation;
    TTimeRef            m_seekTransportLocation;
	
    void stop_disk_thread();
    void check_for_seek_requested();

public slots:
    void seek();


    void set_output_sample_rate(uint outputSampleRate);
    void set_resample_quality(int quality);

private slots:
    bool do_work();

    void private_add_to_work(TAudioSource* source);
    void private_remove_from_work(TAudioSource* source);

signals:
	void seekFinished();
};


#endif

//eof
