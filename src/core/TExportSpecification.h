/*
Copyright (C) 2024 Remon Sijrier

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

#ifndef TEXPORTSPECIFICATION_H
#define TEXPORTSPECIFICATION_H

#include <QObject>
#include <QMap>

#include "TTimeRef.h"

#include <sndfile.h>
#include "gdither_types.h"

class TTimeLineMarker;
class TProject;
class TSheet;

class TExportSpecification : public QObject
{
    Q_OBJECT

public:
    TExportSpecification();
    ~TExportSpecification();

    enum RecordingState {
        NOT_RECORDING,
        RECORDING
    };

    int is_valid();

    int start_export(TProject *project);

    void set_recording_state(int recordingState);
    void set_sample_rate(uint sampleRate);
    void set_channel_count(uint channelCount);
    void set_block_size(uint blockSize);
    void set_render_buffer(audio_sample_t* renderBuffer);
    void set_export_start_location(const TTimeRef &startLocation);
    void set_export_end_location(const TTimeRef &endLocation);

    void add_exported_range(const TTimeRef& time);
    void add_sheet_to_export(TSheet* sheet);

    void set_writer_type(const QString& writerType);
    void set_file_format(int fileFormat);
    void set_data_format(int format);
    void set_sample_rate_conversion_quality(int quality);
    void set_is_cd_export(bool cdExport);
    void set_export_dir(const QString& dir);
    void set_export_file_name(const QString &fileName);
    void set_dither_type(const GDitherType &ditherType);

    void cancel_export();

    bool cancel_export_requested() const {return m_cancelExportRequested;}

    void print_export_data() const;

    int create_cdrdao_toc(TExportSpecification* spec);
    TTimeRef get_cd_totaltime(TExportSpecification*);


    inline void silence_render_buffer(nframes_t nframes) {
        Q_ASSERT(nframes <= m_renderBufferSize);
        memset (get_render_buffer(), 0, sizeof (audio_sample_t) * nframes * get_channel_count());
    }

    int get_recording_state() const {return m_recordingState;}
    uint get_sample_rate() const {return m_sampleRate;}
    uint get_channel_count() const {return m_channelCount;}
    uint get_block_size() const {return m_blockSize;}
    uint get_render_buffer_size() const {return m_renderBufferSize;}
    audio_sample_t* get_render_buffer() const;
    int get_data_format() const {return m_dataFormat;}
    int get_bit_depth() const;
    int get_sample_rate_conversion_quality() const {return m_sampleRateConversionQuality;}
    TTimeRef get_export_start_location() const {return m_exportStartLocation;}
    TTimeRef get_export_end_location() const {return m_exportEndLocation;}
    TTimeRef get_export_location() const {return m_exportLocation;}
    TTimeRef get_export_length() const;
    GDitherSize get_dither_size() const;
    GDitherType get_dither_type() const {return m_ditherType;}
    uint get_sample_bytes() const {return m_sampleBytes;}
    QList<TSheet*> get_sheets_to_export() const {return m_sheetsToExport;}

    bool is_cd_export() const {return m_isCdExport;}

    nframes_t get_remaining_export_frames() const;

    QString get_writer_type() const {return m_writerType;}
    QString get_export_dir() const {return m_exportDir;}
    QString get_export_file_name() const {return m_exportFileName;}
    int get_file_format() const {return m_fileFormat;}

    QString get_file_extension() const;

    QMap<QString, QString>	extraFormat;


    QString		tocFileName;
    QString		cdrdaoToc;
    bool		writeToc;
    bool 		resumeTransport;
    TTimeRef	resumeTransportLocation;

private:
    QList<TSheet* >  m_sheetsToExport;
    int             m_fileFormat;
    uint            m_sampleRate;
    uint            m_channelCount;
    int             m_recordingState;

    TTimeRef		m_exportStartLocation;
    TTimeRef		m_exportEndLocation;
    TTimeRef      	m_exportLocation;

    QString         m_writerType;
    audio_sample_t* m_renderBuffer;
    audio_sample_t* m_setRenderBuffer;
    uint            m_blockSize;
    uint            m_renderBufferSize;

    GDitherType     m_ditherType;

    QString         m_exportDir;
    QString         m_exportFileName;

    bool            m_isCdExport;
    bool            m_cancelExportRequested;

    int             m_dataFormat;
    uint            m_sampleBytes;
    int             m_sampleRateConversionQuality;
    int             m_progress;



    void update_renderbuffer_size();
    void delete_render_buffer();

signals:
    void progressChanged(int);
    void exportMessage(QString);
};


#endif // TEXPORTSPECIFICATION_H
