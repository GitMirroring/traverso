// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TWaveform.h"

#include "TBufferedAudioStreamReader.h"
#include "TFileIOBuffer.h"
#include "TLog.h"
#include "TPeakBuildTask.h"
#include "TPeakCache.h"
#include "TPeakPyramid.h"
#include "TPeakStream.h"
#include "TProject.h"
#include "TProjectManager.h"
#include "TResourcesManager.h"

#include <algorithm>
#include <cmath>

QString TWaveform::peak_path(const TBufferedAudioStream* source)
{
    // In the user's peak cache, named after the audio file's path
    return TPeakCache::path_for(source->get_filename());
}

TWaveform::TWaveform(TBufferedAudioStreamReader* source, RequestBuild build)
    : m_sourceId(source->get_id())
    , m_fileName(source->get_filename())
    , m_rate(source->get_file_rate())
    , m_channelCount(int(source->get_channel_count()))
    , m_frameCount(source->get_nframes())
    , m_sourceValid(source->get_error() >= 0)
    , m_peakPath(peak_path(source))
    , m_decodeBuffer(std::make_unique<TFileIOBuffer>())
    , m_requestBuild(std::move(build))
{
    // Delivered once per build, also when it finished before it was watched
    connect(&m_build, &QFutureWatcherBase::finished, this, &TWaveform::build_finished);
    connect(&m_build, &QFutureWatcherBase::progressValueChanged, this, [this](int percent) {
        m_progress = percent;
        emit progress(percent);
    });

    m_closeReader.setSingleShot(true);
    m_closeReader.setInterval(5000);
    connect(&m_closeReader, &QTimer::timeout, this, [this] {
        delete m_source;
        m_source = nullptr;
    });
}

TWaveform::TWaveform(std::shared_ptr<TPeakStream> live)
    : m_live(std::move(live))
    , m_state(State::Ready)
    , m_opened(true)
{
}

TWaveform::~TWaveform()
{
    // A running build is not cancelled: other waveforms of the same source
    // may wait for it (split clips, a clip that recreates its waveform).
    // Builds are cancelled when the project closes.
    delete m_source;
}

int TWaveform::channel_count() const
{
    if (m_live) {
        return m_live->channel_count();
    }
    return m_channelCount;
}

TPeakFile::SourceInfo TWaveform::source_info() const
{
    return TPeakFile::SourceInfo::from_file(m_fileName, m_rate, m_channelCount, m_frameCount);
}

TBufferedAudioStreamReader* TWaveform::reader()
{
    if (!m_source && m_sourceValid) {
        m_source = resources_manager()->get_readsource(m_sourceId);
        if (m_source && m_source->get_error() < 0) {
            delete m_source;
            m_source = nullptr;
            m_sourceValid = false;
        }
    }
    m_closeReader.start();
    return m_source;
}

TTimeRef TWaveform::recorded_length() const
{
    if (!m_live) {
        return TTimeRef();
    }
    // The frames whose blocks a read has
    return TTimeRef(uint64_t(m_live->written_frames()), m_live->rate());
}

void TWaveform::prepare()
{
    if (!m_opened) {
        m_opened = true;
        open_or_build();
    }
}

TWaveform::State TWaveform::state()
{
    prepare();
    return m_live ? State::Ready : m_state;
}

QString TWaveform::message() const
{
    if (m_state == State::Unsupported) {
        return tr("No waveform: %1 channels, at most %2 are supported").arg(m_channelCount).arg(TPeakBuildTask::MaxChannels);
    }
    return QString();
}

void TWaveform::open_or_build()
{
    if (!m_sourceValid) {
        TLOG_W("peaks", "no readable source for {}", m_peakPath.toStdString());
        m_state = State::Failed;
        return;
    }
    if (!TPeakBuildTask::supports(m_channelCount)) {
        TLOG_W("peaks", "{}: {} channels, no waveform", m_peakPath.toStdString(), m_channelCount);
        m_state = State::Unsupported;
        return;
    }

    const TPeakFile::Status status = m_file.open(m_peakPath, source_info());
    switch (status) {
    case TPeakFile::Status::Ok:
        TLOG_D("peaks", "opened {}", m_peakPath.toStdString());
        // Used now: the cache keeps what is used
        TPeakCache::touch(m_peakPath);
        m_state = State::Ready;
        return;
    case TPeakFile::Status::Error:
        TLOG_W("peaks", "cannot open {}", m_peakPath.toStdString());
        m_state = State::Failed;
        return;
    case TPeakFile::Status::Missing:
    case TPeakFile::Status::Invalid:
        break;
    }

    // A file that was just built and still does not open is not going to
    if (m_builds > 0) {
        TLOG_W("peaks", "{} does not match its source after building it", m_peakPath.toStdString());
        m_state = State::Failed;
        return;
    }

    start_build(status == TPeakFile::Status::Missing ? "no peak file yet" : "the peak file does not match the source");
}

void TWaveform::start_build(const char* reason)
{
    TLOG_I("peaks", "building {} ({})", m_peakPath.toStdString(), reason);
    ++m_builds;
    m_state = State::Building;
    m_progress = 0;
    // No build service (a clip without a sheet): a cancelled future, so the
    // watcher reports a failed build
    const TPeakBuild build = m_requestBuild ? m_requestBuild(m_fileName, m_peakPath, m_rate, m_channelCount) : TPeakBuild{};
    m_building = build.stream;
    m_build.setFuture(build.future);
}

void TWaveform::build_finished()
{
    // The build's peaks go once the file is open (or the build failed): read
    // no more from them. Another build started below brings its own.
    const std::shared_ptr<TPeakStream> built = std::move(m_building);

    // A cancelled build has no result (the project closes, or its DiskIO went)
    const QFuture<TPeakBuildResult> build = m_build.future();
    const TPeakBuildResult result = !build.isCanceled() && build.resultCount() > 0 ? build.result() : TPeakBuildResult{};

    if (result.ok) {
        open_or_build();
    } else if (build.isCanceled() && m_cancelledBuilds++ == 0) {
        // Not failed but cancelled: the DiskIO that ran it went (its sheet
        // was deleted) while this waveform's clip lives on. Ask once more.
        TLOG_I("peaks", "the build of {} was cancelled, asking again", m_peakPath.toStdString());
        m_builds = 0;
        open_or_build();
        if (m_state == State::Building) {
            return;
        }
    } else {
        m_state = State::Failed;
    }
    emit ready();
}

TWaveformColumns::Source TWaveform::columns(const TTimeRef& start, const TTimeRef& columnWidth, int count, TWaveformColumn* out)
{
    const State current = state();
    if (current == State::Building && m_building) {
        // What is built so far (the samples of the source are all there)
        return stream_columns(*m_building, this, start, columnWidth, count, out);
    }
    if (current != State::Ready) {
        return TWaveformColumns::Source::None;
    }
    if (m_live) {
        return stream_columns(*m_live, nullptr, start, columnWidth, count, out);
    }
    return TWaveformColumns::query(m_file.levels(), this, start, columnWidth, count, out);
}

TWaveformColumns::Source TWaveform::stream_columns(const TPeakStream& stream, TWaveformSampleSource* samples,
                                                   const TTimeRef& start, const TTimeRef& columnWidth, int count,
                                                   TWaveformColumn* out)
{
    // The level-0 blocks the columns cover, read from the file: zoomed out
    // far that is all of them (16 MB per channel-hour at 48 kHz, mostly from
    // the page cache), until post-processing gives the file its levels
    const uint rate = stream.rate();
    const qint64 first = TTimeRef::frame_at_or_before(start, rate) >> TPeakLevels::BlockFramesShift;
    const qint64 last = (TTimeRef::frame_at_or_after(start + columnWidth * double(count), rate) >> TPeakLevels::BlockFramesShift) + 1;
    const TPeakLevels levels = stream.read(first, last, m_streamRecords);
    return TWaveformColumns::query(levels, samples, start, columnWidth, count, out);
}

float TWaveform::max_amplitude(const TTimeRef& start, const TTimeRef& end)
{
    if (state() != State::Ready || m_live) {
        return -1.0f;
    }
    return TWaveformColumns::max_amplitude(m_file.levels(), this,
                                           TTimeRef::frame_at_or_after(start, m_rate), TTimeRef::frame_at_or_after(end, m_rate));
}

TTimeRef TWaveform::nearest_zero_crossing(const TTimeRef& time, const TTimeRef& maxDistance)
{
    if (state() != State::Ready || m_live) {
        return TTimeRef(TTimeRef::INVALID);
    }
    const qint64 frame = TWaveformColumns::nearest_zero_crossing(
        m_file.levels(), this, TTimeRef::frame_at_or_before(time, m_rate),
        qint64(std::ceil(TTimeRef::to_frames(maxDistance, m_rate))));
    return frame < 0 ? TTimeRef(TTimeRef::INVALID) : TTimeRef(uint64_t(frame), m_rate);
}

qint64 TWaveform::read(qint64 firstFrame, qint64 count, float* const* channels)
{
    TBufferedAudioStreamReader* source = reader();
    if (!source) {
        return 0;
    }
    const uint channelCount = uint(m_channelCount);
    m_decodeBuffer->check_capacity(uint(count), channelCount);

    const int read = source->file_read(*m_decodeBuffer, nframes_t(firstFrame), nframes_t(count));
    if (read <= 0) {
        return 0;
    }

    const qint64 frames = std::min<qint64>(read, count);
    for (uint c = 0; c < channelCount; ++c) {
        std::copy_n(m_decodeBuffer->get_channel_buffer(c).get_data(nframes_t(frames)), frames, channels[c]);
    }
    return frames;
}
