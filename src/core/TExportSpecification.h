#ifndef TEXPORTSPECIFICATION_H
#define TEXPORTSPECIFICATION_H

#include <QObject>
#include <QMap>
#include "TTimeRef.h"
#include "defines.h"
#include "gdither_types.h"

class TProject;
class TSheet;

class TExportSpecification : public QObject
{
    Q_OBJECT

public:
    TExportSpecification();
    ~TExportSpecification() override;

    enum RecordingState { NOT_RECORDING, RECORDING };

    int is_valid();
    int prepare_export(TProject *project);

    void set_recording_state(int recordingState);
    void set_sample_rate(uint sampleRate);
    void set_channel_count(uint channelCount);
    void set_block_size(uint blockSize);
    void set_export_start_location(const TTimeRef &startLocation);
    void set_export_end_location(const TTimeRef &endLocation);

    // Appends or updates a format-specific encoder parameter key-value pair
    void add_extra_format(const QString& key, const QString& value);

    // Retrieves a format-specific parameter value. Returns defaultValue if key is missing.
    QString get_extra_format(const QString& key, const QString& defaultValue = QString()) const;

    // Checks if a specific extra format parameter key exists
    bool has_extra_format(const QString& key) const;

    // Clears all dynamic format-specific settings
    void clear_extra_formats();


    void add_exported_range(const TTimeRef& time);
    void add_sheet_to_export(TSheet* sheet);
    void clear_sheets_to_export();

    // Type-safe compile-time setters matching the TraversoDAW namespace
    void set_writer_type(TraversoDAW::WriterType writerType) { m_writerType = writerType; }
    void set_file_format(TraversoDAW::FileFormat fileFormat);
    void set_data_format(TraversoDAW::DataFormat format);
    void set_sample_rate_conversion_quality(int quality);
    void set_is_cd_export(bool cdExport);
    void set_export_dir(const QString& dir);
    void set_export_file_name(const QString &fileName);
    void set_dither_type(const GDitherType &ditherType);

    void cancel_export();
    bool cancel_export_requested() const { return m_cancelExportRequested; }

    void print_export_data() const;

    int create_cdrdao_toc(TProject* project, TExportSpecification* spec);
    TTimeRef get_cd_totaltime(TExportSpecification*);

    int get_recording_state() const { return m_recordingState; }
    uint get_sample_rate() const { return m_sampleRate; }
    uint get_channel_count() const { return m_channelCount; }
    uint get_block_size() const { return m_blockSize; }

    // Type-safe compile-time getters
    TraversoDAW::WriterType get_writer_type() const { return m_writerType; }
    TraversoDAW::FileFormat get_file_format() const { return m_fileFormat; }
    TraversoDAW::DataFormat get_data_format() const { return m_dataFormat; }
    void set_bitrate_mode(TraversoDAW::BitrateMode mode) { m_bitrateMode = mode; }
    TraversoDAW::BitrateMode get_bitrate_mode() const { return m_bitrateMode; }

    static TraversoDAW::BitrateMode string_to_format(const QString &option);
    static QString format_to_string(TraversoDAW::BitrateMode format);
    static QString writer_type_to_string(TraversoDAW::WriterType writerType);

    int get_bit_depth() const;
    int get_sample_rate_conversion_quality() const { return m_sampleRateConversionQuality; }
    TTimeRef get_export_start_location() const { return m_exportStartLocation; }
    TTimeRef get_export_end_location() const { return m_exportEndLocation; }
    TTimeRef get_export_location() const { return m_exportLocation; }
    TTimeRef get_export_length() const;
    GDitherSize get_dither_size() const;
    GDitherType get_dither_type() const { return m_ditherType; }
    uint get_sample_bytes() const { return m_sampleBytes; }
    QList<TSheet*> get_sheets_to_export() const { return m_sheetsToExport; }

    bool is_cd_export() const { return m_isCdExport; }
    nframes_t get_remaining_export_frames() const;

    QString get_export_dir() const { return m_exportDir; }
    QString get_export_file_name() const { return m_exportFileName; }
    QString get_file_extension() const;

    bool 		resumeTransport;
    TTimeRef	resumeTransportLocation;
    bool                        writeToc;
    QString                     tocFileName;

private:
    QList<TSheet* >            m_sheetsToExport;
    TraversoDAW::FileFormat    m_fileFormat{TraversoDAW::FileFormat::UNKNOWN};
    QMap<QString, QString>      m_extraFormat;
    uint                       m_sampleRate;
    uint                       m_channelCount;
    int                        m_recordingState;

    TTimeRef                   m_exportStartLocation;
    TTimeRef                   m_exportEndLocation;
    TTimeRef                   m_exportLocation;

    TraversoDAW::WriterType    m_writerType;
    TraversoDAW::BitrateMode   m_bitrateMode;
    uint                       m_blockSize;

    GDitherType                m_ditherType;
    QString                    m_exportDir;
    QString                    m_exportFileName;

    QString                     cdrdaoToc;

    bool                       m_isCdExport;
    bool                       m_cancelExportRequested;

    TraversoDAW::DataFormat    m_dataFormat;
    uint                       m_sampleBytes;
    int                        m_sampleRateConversionQuality;
    int                        m_progress;


signals:
    void progressChanged(int);
    void exportMessage(QString);
};

#endif // TEXPORTSPECIFICATION_H
