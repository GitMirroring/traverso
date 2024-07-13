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

#ifndef DISKIO_H
#define DISKIO_H

#include <QMutex>
#include <QList>
#include <QPair>
#include <QThread>

#include "RingBufferNPT.h"
#include "defines.h"

class ReadSource;
class WriteSource;
class AudioSource;
class DiskIOThread;
class Sheet;
class DecodeBuffer;

// DiskIOThread is a private class to be used by
// DiskIO only for processing read/write buffers
// in a seperate thread.
class DiskIOThread : public QThread
{
    Q_OBJECT
public:
    DiskIOThread();

protected:
    void run() override;
};


class DiskIO : public QObject
{
	Q_OBJECT

public:
	DiskIO(Sheet* sheet);
	~DiskIO();
	
	static const int writebuffertime = 5;
	static const int bufferdividefactor = 5;

	void prepare_for_seek();
    void set_output_rate(uint rate);

	void register_read_source(ReadSource* source);
	void register_write_source(WriteSource* source);
	
	void set_resample_quality(int quality);
	
	void unregister_read_source(ReadSource* source);
	void unregister_write_source(WriteSource* source);

        float get_cpu_time();
	int get_write_buffers_fill_status();
	int get_read_buffers_fill_status();
    uint get_output_rate() {return m_outputRate;}
	int get_resample_quality() {return m_resampleQuality;}
	DecodeBuffer* get_resample_decode_buffer() {return m_resampleDecodeBuffer;}

private:
    Sheet*              m_sheet;
    std::atomic<bool>   m_stopWork;
    std::atomic<bool>   m_seeking;

    QList<ReadSource*>	m_readSources;
    QList<ReadSource*>	m_processableReadSources;
    QList<ReadSource*>	m_processableSyncSources;
    QList<WriteSource*>	m_processableWriteSources;
    QList<WriteSource*>	m_writeSources;

    QList<QPair<int, WriteSource*> > m_writersStatus;

    DiskIOThread		m_diskThread;
        QMutex			mutex;
    std::atomic<int>    m_readBufferFillStatus;
    std::atomic<int>    m_writeBufferFillStatus;

    trav_time_t         m_totalDoWorkTime{};
    trav_time_t         m_doWorkStartTime{};
    trav_time_t 		m_lastdoWorkReadTime;
    RingBufferNPT<trav_time_t>*	m_cpuTime;
    int                 m_resampleQuality;
    bool                m_resampleQualityChanged;
    bool                m_sampleRateChanged;
    int                 m_hardDiskOverLoadCounter;
    audio_sample_t*		framebuffer;
    audio_sample_t*		m_readbuffer{};
    DecodeBuffer*		m_fileDecodeBuffer;
    DecodeBuffer*		m_resampleDecodeBuffer;
    uint                m_outputRate{};

	
    void update_time_usage(trav_time_t time);
	
	int there_are_processable_sources();

    void stop_disk_thread();

public slots:
    void seek();

private slots:
        void do_work();

signals:
	void seekFinished();
	void readSourceBufferUnderRun();
    void writeSourceBufferOverRun();
    void ioStartRequested();
    void ioStopRequested();

};


#endif

//eof
