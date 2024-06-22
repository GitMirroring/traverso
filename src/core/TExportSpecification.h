#ifndef TEXPORTSPECIFICATION_H
#define TEXPORTSPECIFICATION_H

#include <QObject>
#include <QMap>

#include "TTimeRef.h"

#include <sndfile.h>
#include "gdither_types.h"

class Marker;
class TExportThread;

class TExportSpecification : public QObject
{
    Q_OBJECT

public:
    TExportSpecification();
    ~TExportSpecification();

    enum RenderPass {
        CALC_NORM_FACTOR,
        WRITE_TO_HARDDISK,
        CREATE_CDRDAO_TOC
    };

    enum RecordingState {
        NOT_RECORDING,
        RECORDING
    };

    int is_valid();

    void set_recording_state(int recordingState);
    void set_sample_rate(uint sampleRate);
    void set_channel_count(uint channelCount);
    void set_block_size(uint blockSize);
    void set_render_buffer(audio_sample_t* renderBuffer);
    void set_export_start_location(TTimeRef startLocation);
    void set_export_end_location(TTimeRef endLocation);
    void add_exported_frames(nframes_t frames);

    void set_writer_type(const QString& writerType);
    void set_file_format(int fileFormat);
    void set_data_format(int format);
    void set_sample_rate_conversion_quality(int quality);
    void set_is_cd_export(bool cdExport);

    void print_export_data() const;

    void update_peak_value();

    inline void silence_render_buffer(nframes_t nframes) {
        Q_ASSERT(nframes <= m_renderBufferSize);
        memset (get_render_buffer(), 0, sizeof (audio_sample_t) * nframes * get_channel_count());
    }

    int get_recording_state() const {return m_recordingState;}
    uint get_sample_rate() const {return m_sampleRate;}
    uint get_channel_count() const {return m_channelCount;}
    uint get_block_size() const {return m_blockSize;}
    uint get_render_buffer_size() const {return m_renderBufferSize;}
    audio_sample_t* get_render_buffer();
    int get_data_format() const {return m_dataFormat;}
    int get_bit_depth() const;
    int get_sample_rate_conversion_quality() const {return m_sampleRateConversionQuality;}
    TTimeRef get_export_start_location() const {return m_exportStartLocation;}
    TTimeRef get_export_end_location() const {return m_exportEndLocation;}
    TTimeRef get_export_location() const {return m_exportLocation;}
    TTimeRef get_export_length() const;

    bool is_cd_export() const {return m_isCdExport;}

    nframes_t get_remaining_export_frames() const;

    QString get_writer_type() const {return m_writerType;}
    int get_file_format() const {return m_fileFormat;}

    const char* get_file_extension() const;

    GDitherType     dither_type;

    QMap<QString, QString>	extraFormat;

    bool  		stop;      /* UI sets this */
    bool		breakout;
    bool  		running;   /* audio thread sets to false when export is done */

    int   		status;
    bool		allSheets;
    QString		exportdir;
    QString		basename;
    QString		name;
    QString		tocFileName;
    QString		cdrdaoToc;
    bool		writeToc;
    bool		normalize;
    int         renderpass;
    float		m_peakValue;
    float 		normvalue;
    bool 		resumeTransport;
    TTimeRef	resumeTransportLocation;
    bool		renderfinished;

    TExportThread* 	thread;

private:
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

    bool            m_isCdExport;

    int             m_dataFormat;
    int             m_sampleRateConversionQuality;
    int             m_progress;

    void update_renderbuffer_size();
    void delete_render_buffer();

signals:
    void progressChanged(int);
};


#endif // TEXPORTSPECIFICATION_H
