// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TWaveform.h"

#include "TBufferedAudioStreamReader.h"
#include "TFileIOBuffer.h"
#include "TLog.h"
#include "TPeakPyramid.h"
#include "TPeakRecording.h"
#include "TProject.h"
#include "TProjectManager.h"
#include "TResourcesManager.h"

#include <algorithm>
#include <cmath>

QString TWaveform::peak_path(const TBufferedAudioStream* source)
{
    // Next to the project's audio sources, like the old peak files
    QString path;
    if (TProject* project = pm().get_project()) {
        path = project->get_root_dir() + "/peakfiles/";
    } else {
        path = source->get_dir();
        path.replace("audiosources", "peakfiles");
    }
    return path + source->get_name() + ".peaks";
}

TWaveform::TWaveform(TBufferedAudioStreamReader* source, PeakJobs jobs)
    : m_sourceId(source->get_id())
    , m_fileName(source->get_filename())
    , m_rate(source->get_file_rate())
    , m_channelCount(int(source->get_channel_count()))
    , m_frameCount(source->get_nframes())
    , m_sourceValid(source->get_error() >= 0)
    , m_peakPath(peak_path(source))
    , m_decodeBuffer(std::make_unique<TFileIOBuffer>())
    , m_jobs(std::move(jobs))
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

TWaveform::TWaveform(std::shared_ptr<TPeakRecording> live)
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
    // Whole blocks: the waveform ends at the last complete one
    const qint64 frames = (m_live->recorded_frames() >> TPeakLevels::BlockFramesShift) << TPeakLevels::BlockFramesShift;
    return TTimeRef(uint64_t(frames), m_live->rate());
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

void TWaveform::open_or_build()
{
    if (!m_sourceValid) {
        TLOG_W("peaks", "no readable source for {}", m_peakPath.toStdString());
        m_state = State::Failed;
        return;
    }

    const TPeakFile::Status status = m_file.open(m_peakPath, source_info());
    switch (status) {
    case TPeakFile::Status::Ok:
        TLOG_D("peaks", "opened {}", m_peakPath.toStdString());
        m_state = State::Ready;
        if (m_builds == 0 && m_jobs.verify) {
            // Found on disk: shown at once, its blocks are checked (and read
            // into the page cache) in DiskIO's idle time. A damaged cache file
            // shows a wrong waveform until it is built again, which is
            // harmless; waiting with an empty clip is not. A file this session
            // built or checked passes at once.
            m_verifying = true;
            m_build.setFuture(m_jobs.verify(m_peakPath, source_info()));
        }
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
    m_build.setFuture(m_jobs.build ? m_jobs.build(m_fileName, m_peakPath) : QFuture<TPeakBuildResult>());
}

void TWaveform::build_finished()
{
    // A cancelled build has no result (the project closes, or its DiskIO went)
    const QFuture<TPeakBuildResult> build = m_build.future();
    const TPeakBuildResult result = !build.isCanceled() && build.resultCount() > 0 ? build.result() : TPeakBuildResult{};

    if (m_verifying) {
        m_verifying = false;
        // Not damaged, or nobody could check it (its DiskIO went; its layout
        // is right): it stays in use
        if (!result.ok && !build.isCanceled()) {
            m_file.close();
            start_build("the peak file is damaged");
            emit ready();   // the views paint the build instead
        }
        return;
    }

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
    if (state() != State::Ready) {
        return TWaveformColumns::Source::None;
    }
    if (m_live) {
        // The recording's memory stays put during the read only
        TWaveformColumns::Source source = TWaveformColumns::Source::None;
        m_live->read([&](const TPeakLevels& levels) {
            source = TWaveformColumns::query(levels, nullptr, start, columnWidth, count, out);
        });
        return source;
    }
    return TWaveformColumns::query(m_file.levels(), this, start, columnWidth, count, out);
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
