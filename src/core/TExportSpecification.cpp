#include "TExportSpecification.h"

#include "Utils.h"
#include "Mixer.h"

#include <samplerate.h>
#include <sndfile.h>

#include "Debugger.h"

TExportSpecification::TExportSpecification()
{
    m_sampleRate = 44100;
    m_channelCount = 0;
    m_blockSize = 16384;

    m_writerType = "sndfile";
    m_fileFormat = SF_FORMAT_WAV;
    m_sampleRateConversionQuality = SRC_SINC_MEDIUM_QUALITY;

    m_exportStartLocation = TTimeRef::negative_max_length();
    m_exportEndLocation = TTimeRef::negative_max_length();
    m_exportLocation = TTimeRef();

    m_renderBuffer = nullptr;
    m_setRenderBuffer = nullptr;

    m_progress = 0;

    dither_type = GDitherShaped;
    m_dataFormat = SF_FORMAT_FLOAT;

    allSheets = false;
    stop = false;
    breakout = false;
    // This state is the default and used for
    // to for conversion/copying of audio files
    // Set to RecordingState::RECORDING to use this ExportSpecification
    // to record an audio file for an AudioClip
    m_recordingState = RecordingState::NOT_RECORDING;
    exportdir = "";
    basename = "";
    name = "";
    writeToc = false;
    normalize = false;
    renderpass = WRITE_TO_HARDDISK;
    normvalue = 1.0;
    m_peakValue = 0.0;
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

    if (m_exportStartLocation == TTimeRef::negative_max_length()) {
        printf("ExportSpecification: No start frame configured!\n");
        return -1;
    }

    if (m_exportEndLocation == TTimeRef::negative_max_length()) {
        printf("ExportSpecification: No end frame configured!\n");
        return -1;
    }

    if (! m_renderBuffer ) {
        printf("ExportSpecification: No mixdown buffer created!!\n");
        return -1;
    }

    if (exportdir.isEmpty()) {
        printf("ExportSpecification: No export dir configured!\n");
        return -1;
    }

    if (name.isEmpty()) {
        printf("ExportSpecification: No name configured!\n");
        return -1;
    }

    return 1;
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

void TExportSpecification::add_exported_frames(nframes_t frames)
{
    m_exportLocation.add_frames(frames, m_sampleRate);

    int progress = int(((get_export_location() - get_export_start_location()) / get_export_length()) * 100);
    if (progress > m_progress) {
        m_progress = progress;
        emit progressChanged(m_progress);
        printf("progress %d\n", progress);
    }
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
    PMESG("Base Name %s", QS_C(basename));
    PMESG("Name %s", QS_C(name));

    PMESG("Progress %d", m_progress);

}

void TExportSpecification::update_peak_value()
{
    m_peakValue = Mixer::compute_peak(get_render_buffer(), get_render_buffer_size(), m_peakValue);
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

nframes_t TExportSpecification::get_remaining_export_frames() const
{
    return (m_exportEndLocation - m_exportLocation).to_frame(m_sampleRate);
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
