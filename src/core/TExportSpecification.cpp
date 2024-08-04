#include "TExportSpecification.h"

#include "Information.h"
#include "Utils.h"

#include <samplerate.h>
#include <sndfile.h>

#include "Debugger.h"
#include "qdir.h"

TExportSpecification::TExportSpecification()
{
    m_sampleRate = 44100;
    m_channelCount = 0;
    m_blockSize = 1024;

    m_writerType = "sndfile";
    m_fileFormat = SF_FORMAT_WAV;
    m_sampleRateConversionQuality = SRC_SINC_MEDIUM_QUALITY;

    m_exportStartLocation = TTimeRef::INVALID;
    m_exportEndLocation = TTimeRef::INVALID;
    m_exportLocation = TTimeRef();

    m_renderBuffer = nullptr;
    m_setRenderBuffer = nullptr;

    m_progress = 0;

    m_ditherType = GDitherShaped;
    set_data_format(SF_FORMAT_FLOAT);

    m_cancelExportRequested = false;
    // This state is the default and used for
    // to for conversion/copying of audio files
    // Set to RecordingState::RECORDING to use this ExportSpecification
    // to record an audio file for an AudioClip
    m_recordingState = RecordingState::NOT_RECORDING;
    m_exportDir = "";
    m_exportFileName = "";
    writeToc = false;
    m_isCdExport = false;
}

TExportSpecification::~TExportSpecification()
{
    delete_render_buffer();
}

int TExportSpecification::is_valid()
{
    if (m_channelCount == 0) {
        printf("ExportSpecification: No channels configured!\n");
        return -1;
    }

    if (m_exportStartLocation == TTimeRef::INVALID) {
        printf("ExportSpecification: No start frame configured!\n");
        return -1;
    }

    if (m_exportEndLocation == TTimeRef::INVALID) {
        printf("ExportSpecification: No end frame configured!\n");
        return -1;
    }

    // if (get_export_length() == TTimeRef()) {
    //     info().warning(tr("No audio to export! (Is everything muted?)"));
    //     return -1;
    // }

    if (get_export_start_location() > get_export_end_location()) {
        info().warning(tr("Export start frame starts beyond export end frame!!"));
        return -1;
    }

    if (! m_renderBuffer ) {
        printf("ExportSpecification: No mixdown buffer created!!\n");
        return -1;
    }

    if (m_exportDir.isEmpty()) {
        printf("ExportSpecification: No export dir configured!\n");
        return -1;
    }

    if (m_exportFileName.isEmpty()) {
        printf("ExportSpecification: No name configured!\n");
        return -1;
    }

    return 1;
}

int TExportSpecification::start_export(Project* project)
{

    QDir dir(m_exportDir);
    if (!m_exportDir.isEmpty() && !dir.exists()) {
        if (!dir.mkpath(m_exportDir)) {
            QString message = tr("Creating Export Directory failed: %1").arg(m_exportDir);
            info().warning(message);
            emit exportMessage(message);
            return -1;
        }
    }

    m_cancelExportRequested = false;

    return 0;
}

void TExportSpecification::set_recording_state(int recordingState)
{
    PMESG("Setting Recording State to %d", recordingState);

    m_recordingState = recordingState;
}

void TExportSpecification::set_sample_rate(uint sampleRate)
{
    Q_ASSERT(sampleRate != 0);

    m_sampleRate = sampleRate;
}

void TExportSpecification::set_channel_count(uint channelCount)
{
    Q_ASSERT(channelCount != 0);

    m_channelCount = channelCount;
    update_renderbuffer_size();
}

void TExportSpecification::set_block_size(uint blockSize)
{
    Q_ASSERT(blockSize > 0);
    Q_ASSERT(is_power_of_two(blockSize));

    m_blockSize = blockSize;
    update_renderbuffer_size();
}

void TExportSpecification::set_render_buffer(audio_sample_t *renderBuffer)
{
    m_setRenderBuffer = renderBuffer;
}

void TExportSpecification::set_export_start_location(TTimeRef startLocation)
{
    m_exportStartLocation = startLocation;
    m_exportLocation = startLocation;
    m_progress = 0;
    PMESG("Setting Start Location to (minutes:seconds) %s", QS_C(TTimeRef::timeref_to_ms_3(m_exportStartLocation)));
}

void TExportSpecification::set_export_end_location(TTimeRef endLocation)
{
    m_exportEndLocation = endLocation;
    PMESG("Setting End Location to (minutes:seconds) %s", QS_C(TTimeRef::timeref_to_ms_3(m_exportEndLocation)));
}

void TExportSpecification::add_exported_range(const TTimeRef& time)
{
    m_exportLocation += time;

    int progress = int(((get_export_location() - get_export_start_location()) / get_export_length()) * 100);
    if (progress > m_progress) {
        m_progress = progress;
        emit progressChanged(m_progress);
    }
}

void TExportSpecification::add_sheet_to_export(Sheet *sheet)
{
    if (m_sheetsToExport.contains(sheet)) {
        return;
    }
    m_sheetsToExport.append(sheet);
}

void TExportSpecification::set_writer_type(const QString &writerType)
{
    Q_ASSERT(writerType == "sndfile" || writerType == "wavpack");

    m_writerType = writerType;
}

void TExportSpecification::set_file_format(int fileFormat)
{
    Q_ASSERT(fileFormat == SF_FORMAT_WAV
             || fileFormat == SF_FORMAT_AIFF
             || fileFormat == SF_FORMAT_W64
             || fileFormat ==SF_FORMAT_FLAC
             || fileFormat == SF_FORMAT_OGG
             || fileFormat == SF_FORMAT_MPEG);
    m_fileFormat = fileFormat;
    PMESG("Setting file format to %s", get_file_extension());
}

const char* TExportSpecification::get_file_extension() const
{
    switch(m_fileFormat)
    {
    case SF_FORMAT_WAV: return ".wav";
    case SF_FORMAT_AIFF: return ".aiff";
    case SF_FORMAT_W64: return ".w64";
    case SF_FORMAT_FLAC: return ".flac";
    case SF_FORMAT_OGG: return ".ogg";
    case SF_FORMAT_MPEG: return ".mp3";
    // case SF_FORMAT_WAVPACK: return ".wv"; // libsndfile does not support wavpack (yet?)
    default: PERROR("File format not supported");
    }

    return ".raw";
}

void TExportSpecification::set_data_format(int format)
{
    Q_ASSERT(format == SF_FORMAT_FLOAT
             || format == SF_FORMAT_PCM_S8
             || format == SF_FORMAT_PCM_16
             || format ==SF_FORMAT_PCM_24
             || format == SF_FORMAT_PCM_32);

    m_dataFormat = format;

    switch (m_dataFormat) {
    case SF_FORMAT_PCM_S8:
        m_sampleBytes = 1;
        break;

    case SF_FORMAT_PCM_16:
        m_sampleBytes = 2;
        break;

    case SF_FORMAT_PCM_24:
    case SF_FORMAT_PCM_32:
        m_sampleBytes = 4;
        break;

    default:
        m_sampleBytes = 0; // float format
        break;
    }
}

void TExportSpecification::set_sample_rate_conversion_quality(int quality)
{
    Q_ASSERT(quality >= SRC_SINC_BEST_QUALITY && quality <= SRC_LINEAR);

    // SRC_SINC_BEST_QUALITY		= 0,
    // SRC_SINC_MEDIUM_QUALITY		= 1,
    // SRC_SINC_FASTEST			= 2,
    // SRC_ZERO_ORDER_HOLD			= 3,
    // SRC_LINEAR					= 4,

    m_sampleRateConversionQuality = quality;
}

void TExportSpecification::set_is_cd_export(bool cdExport)
{
    m_isCdExport = cdExport;
}

void TExportSpecification::set_export_dir(const QString &dir)
{
    m_exportDir = dir;
}

void TExportSpecification::set_export_file_name(const QString &fileName)
{
    m_exportFileName = fileName;
}

void TExportSpecification::set_dither_type(const GDitherType &ditherType)
{
    m_ditherType = ditherType;
}

void TExportSpecification::cancel_export()
{
    m_cancelExportRequested = true;
}

void TExportSpecification::print_export_data() const
{
    PMESG("Starting export, samplerate %d, bitdepth %d, file extension %s",
          get_sample_rate(),
          get_bit_depth(),
          get_file_extension() );
    PMESG("Export State:");
    PMESG("Start Location %s", QS_C(TTimeRef::timeref_to_hms(m_exportStartLocation)));
    PMESG("End Location (minutes:seconds) %s", QS_C(TTimeRef::timeref_to_ms(m_exportEndLocation)));
    PMESG("Export Location (minutes:seconds) %s", QS_C(TTimeRef::timeref_to_ms(m_exportLocation)));
    PMESG("Sample Rate %d", m_sampleRate);
    PMESG("Channel Count %d", m_channelCount);
    PMESG("Block Size %d", m_blockSize);
    PMESG("Writer Type %s", QS_C(m_writerType));
    PMESG("Is CD Export %s", m_isCdExport ? "Yes" : "No");
    PMESG("Export Directory %s", QS_C(m_exportDir));
    PMESG("Export File Name %s", QS_C(m_exportFileName));

    PMESG("Progress %d", m_progress);

}

audio_sample_t *TExportSpecification::get_render_buffer()
{
    if (m_setRenderBuffer) {
        return m_setRenderBuffer;
    }

    return m_renderBuffer;
}

int TExportSpecification::get_bit_depth() const
{
    switch(m_dataFormat)
    {
    case SF_FORMAT_FLOAT: return 32;
    case SF_FORMAT_PCM_S8: return 8;
    case SF_FORMAT_PCM_16: return 16;
    case SF_FORMAT_PCM_24: return 24;
    case SF_FORMAT_PCM_32: return 32;
    default:
    // this cannot be possible
        printf("Impossible situation in TExportSpecification::get_bit_depth, m_dataFormat contains an unknown format");
    }

    return 32;
}

TTimeRef TExportSpecification::get_export_length() const
{
    return m_exportEndLocation - m_exportStartLocation;
}

GDitherSize TExportSpecification::get_dither_size() const
{
    GDitherSize ditherSize;

    switch (get_data_format()) {
    case SF_FORMAT_PCM_S8:
        ditherSize = GDither8bit;
        break;

    case SF_FORMAT_PCM_16:
        ditherSize = GDither16bit;
        break;

    case SF_FORMAT_PCM_24:
        ditherSize = GDither32bit;
        break;

    default:
        ditherSize = GDitherFloat;
        break;
    }

    return ditherSize;
}

nframes_t TExportSpecification::get_remaining_export_frames() const
{
    return TTimeRef::to_frame(m_exportEndLocation - m_exportLocation, m_sampleRate);
}

void TExportSpecification::update_renderbuffer_size()
{
    delete_render_buffer();

    if (m_channelCount == 0) {
        PMESG("TExportSpecification::update_renderbuffer_size(): channel count == 0, not allocating new render buffer");
        // no channel count set, no need to allocate render buffer
        return;
    }

    Q_ASSERT(m_blockSize > 0);
    Q_ASSERT(m_channelCount > 0);

    m_renderBufferSize = m_blockSize * m_channelCount;
    m_renderBuffer = new audio_sample_t[m_renderBufferSize];

    PMESG("TExportSpecification::update_renderbuffer_size(): Allocated renderbuffer of size %d", m_renderBufferSize);
}

void TExportSpecification::delete_render_buffer()
{
    if (!m_renderBuffer) {
        return;
    }

    PMESG("TExportSpecification::update_renderbuffer_size(): Deleting render buffer");
    delete [] m_renderBuffer;
    m_renderBuffer = nullptr;
    m_renderBufferSize = 0;
}

int TExportSpecification::create_cdrdao_toc(TExportSpecification* spec)
{
    // QList<Sheet* > sheets;
    // QString filename = spec->get_export_dir();

    // if (spec->allSheets) {
    //     foreach(Sheet* sheet, m_sheets) {
    //         sheets.append(sheet);
    //     }

    //     // filename of the toc file is "project-name.toc"
    //     filename += get_title() + ".toc";
    // } else {
    //     Sheet* sheet = qobject_cast<Sheet*>(get_current_session());
    //     if (!sheet) {
    //         return -1;
    //     }
    //     sheets.append(sheet);

    //     // filename of the toc file is "sheet-name.toc"
    //     filename += spec->get_export_file_name() + ".toc";
    // }

    // QString output;

    // output += "CD_DA\n\n";
    // output += "CD_TEXT {\n";

    // output += "  LANGUAGE_MAP {\n    0 : EN\n  }\n\n";

    // output += "  LANGUAGE 0 {\n";
    // output += "    TITLE \"" + get_title() +  "\"\n";
    // output += "    PERFORMER \"" + get_performer() + "\"\n";
    // output += "    DISC_ID \"" + get_discid() + "\"\n";
    // output += "    UPC_EAN \"" + get_upc_ean() + "\"\n\n";

    // output += "    ARRANGER \"" + get_arranger() + "\"\n";
    // output += "    SONGWRITER \"" + get_songwriter() + "\"\n";
    // output += "    MESSAGE \"" + get_message() + "\"\n";
    // output += "    GENRE \"" + QString::number(get_genre()) + "\"\n  }\n}\n\n";


    // bool pregap = true;

    // foreach(Sheet* sheet, sheets) {
    //     // if (sheet->prepare_export(spec) < 0) {
    //     //     return -1;
    //     // }
    //     output += sheet->get_timeline()->get_cdrdao_tracklist(spec, pregap);
    //     pregap = false; // only add the pregap at the first sheet
    // }


    // if (spec->writeToc) {
    //     spec->tocFileName = filename;

    //     QFile file(filename);

    //     if (file.open(QFile::WriteOnly)) {
    //         printf("Saving cdrdao toc-file to %s\n", QS_C(spec->tocFileName));
    //         QTextStream out(&file);
    //         out << output;
    //         file.close();
    //     }
    // }

    // spec->cdrdaoToc = output;

    return 1;
}

/* returns the total time of the data that will be written to CD */
TTimeRef TExportSpecification::get_cd_totaltime(TExportSpecification* spec)
{
    // TODO
    // Used to be called from CDWritingDialog::sheet_mode_changed(bool b)
    // but that one needs investigation as well on usefullness.
    TTimeRef totalTime = TTimeRef();

    // spec->renderpass = TExportSpecification::CREATE_CDRDAO_TOC;

    // if (spec->allSheets) {
    //     foreach(Sheet* sheet, m_sheets) {
    //         sheet->prepare_export(spec);
    //         totalTime += spec->totalTime;
    //     }
    // } else {
    //     Sheet* sheet = qobject_cast<Sheet*>(get_current_session());
    //     if (sheet) {
    //         sheet->prepare_export(spec);
    //         totalTime += spec->totalTime;
    //     }
    // }

    return totalTime;
}
