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

#include "TTimeRef.h"
#include "defines.h"

class TReadAudioSource;
class TAudioSource;
class TPeak;
class TPPThread;
class TFileDecodeBuffer;
class PeakDataReader;

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
    static const int ZOOM_LEVELS = 22;
	static const int SAVING_ZOOM_FACTOR = 8;
	static const int MAX_ZOOM_USING_SOURCEFILE = SAVING_ZOOM_FACTOR - 1;
	// Use ~ 1/4 the range of peak_data_t (== short) so we have headroom
	// for samples in the range [-4, +4] or + 12 dB
	static const int MAX_DB_VALUE = 8000;
    constexpr static int zoomStep[ZOOM_LEVELS + 1]= {
        // non-cached zoomlevels.
        1, 2, 4, 8, 12, 16, 24, 32,
        // Cached zoomlevels
        64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536, 131072/*, 262144, 524288, 1048576*/
      // , 64 - 192 -  576,    577 - 1728 - 5184,   5185 - 15552 - 46656
    };


    explicit TPeak(TAudioSource* source);
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
	TReadAudioSource* 	m_source;
	bool 		m_peaksAvailable;
	bool		m_permanentFailure;
	bool		m_interuptPeakBuild;
	static QHash<int, int> chacheIndexLut;
	
	struct ProcessData {
		ProcessData() {
			normValue = peakUpperValue = peakLowerValue = 0;
			processBufferSize = progress = normProcessedFrames = normDataCount = 0;
			nextDataPointLocation = processRange;
		}
		
		audio_sample_t		peakUpperValue;
		audio_sample_t		peakLowerValue;
		audio_sample_t		normValue;
		
		TTimeRef			stepSize;
		TTimeRef			processRange;
		TTimeRef			processLocation;
		TTimeRef			nextDataPointLocation;
		
		nframes_t		normProcessedFrames;
		
		int 			progress;
		int			processBufferSize;
		int			normDataCount;
	};
	
	struct PeakHeaderData {
		int headerSize;
		int normValuesDataOffset;
		int peakDataOffsets[ZOOM_LEVELS - SAVING_ZOOM_FACTOR];
		int peakDataSizeForLevel[ZOOM_LEVELS - SAVING_ZOOM_FACTOR];
        char label[6];	//TPFxxx -> Traverso TPeak File version x.x.x
		int version[2];
	};

	struct ChannelData {
		ChannelData() {
			peakdataDecodeBuffer = 0;
		}
		~ChannelData();
		QString		fileName;
		QString		normFileName;
		QFile 		file;
		QFile		normFile;
		PeakHeaderData	headerdata;
		PeakDataReader*	peakreader;
		ProcessData* 	pd;
		TFileDecodeBuffer*	peakdataDecodeBuffer;
		QHash<uchar *, QPair<int /*offset*/, int /*handle|len*/> > maps;
	};
	
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

class PeakDataReader
{
public:
    PeakDataReader(TPeak::ChannelData* data);
	~PeakDataReader(){};

	nframes_t read_from(TFileDecodeBuffer* buffer, nframes_t start, nframes_t count);

private:
    TPeak::ChannelData* m_d;
	nframes_t	m_readPos;
	nframes_t	m_nframes;

	bool seek(nframes_t start);
	nframes_t read(TFileDecodeBuffer* buffer, nframes_t frameCount);
};

inline QHash< int, int > * TPeak::cache_index_lut()
{
	if(chacheIndexLut.isEmpty()) {
		calculate_lut_data();
	}
	return &chacheIndexLut;
}


#endif

//eof
