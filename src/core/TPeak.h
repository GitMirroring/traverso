/*
Copyright (C) 2005-2024 Remon Sijrier

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

#ifndef TPEAK_H
#define TPEAK_H

#include <QObject>
#include <QThread>
#include <QMutex>
#include <QQueue>
#include <QWaitCondition>
#include <QFile>
#include <QHash>
#include <QPair>
#include <atomic>

#include "TTimeRef.h"
#include "defines.h"

class TBufferedAudioStreamReader;
class TBufferedAudioStream;
class TPeak;
class TPPThread;
class PeakDataReader;
struct ChannelData;

class TPeakProcessor : public QObject
{
	Q_OBJECT	
	
public:
    void queue_task(TPeak* peak);
    void free_peak(TPeak* peak);

private:
    TPPThread* m_ppthread;
	QMutex m_mutex;
	QWaitCondition m_wait;
	bool m_taskRunning;
    TPeak* m_runningPeak;
		
    QQueue<TPeak* > m_queue;
	
	void dequeue_queue();
	
    TPeakProcessor();
    ~TPeakProcessor();
    TPeakProcessor(const TPeakProcessor&);
	// allow this function to create one instance
    friend TPeakProcessor& pp();
	
private slots:
	void start_task();
	
signals:
	void newTask();

};

class TPPThread : public QThread
{
public:
    TPPThread(TPeakProcessor* pp);
	
protected:
	void run();
	
private:
    TPeakProcessor* m_pp;
};


// use this function to access the PeakBuildThread
TPeakProcessor& pp();


class TPeak : public QObject
{
	Q_OBJECT

public:
    static const int ZOOM_LEVELS = 20; // so  21 levels
    static const int SAVING_ZOOM_FACTOR = 8;
    static const int MAX_ZOOM_USING_SOURCEFILE = SAVING_ZOOM_FACTOR - 1;
    static const int MAX_DB_VALUE = 8000;

    constexpr static int zoomStep[ZOOM_LEVELS + 1] = {
        // Non-cached zoomlevels
        1, 2, 4, 8, 12, 16, 24, 32,
        // Cached zoomlevels
        64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536, 131072, 262144
    };


    explicit TPeak(TBufferedAudioStream* source);
    ~TPeak();

	enum { 	NO_PEAKDATA_FOUND = -1,
		NO_PEAK_FILE = -2,
  		PERMANENT_FAILURE = -3
	};
		
	void process(uint channel, const audio_sample_t* buffer, nframes_t frames);
    int prepare_processing(uint rate);
	int finish_processing();
    int calculate_peaks(int chan, float* &buffer, const TTimeRef &startlocation, int peakDataCount, qreal framesPerPeak);

	void close();
	
	void start_peak_loading();

    audio_sample_t get_max_amplitude(const TTimeRef &startlocation, const TTimeRef &endlocation);
	
	static QHash<int, int>* cache_index_lut();
	static int max_zoom_value();

private:
    TBufferedAudioStreamReader* 	m_source;
	bool 		m_peaksAvailable;
	bool		m_permanentFailure;
	bool		m_interuptPeakBuild;
	std::atomic<bool>	m_peakBuildRunning;
	static QHash<int, int> chacheIndexLut;
	



	QList<ChannelData* >	m_channelData;
	
	int create_from_scratch();
	int read_header();
	int write_header(ChannelData* data);
	static void calculate_lut_data();

    friend class TPeakProcessor;
	friend class PeakDataReader;

signals:
	void finished();
	void progress(int m_progress);
};




#endif

//eof
