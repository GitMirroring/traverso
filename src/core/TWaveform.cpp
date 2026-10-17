// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TWaveform.h"

#include "TBufferedAudioStreamReader.h"
#include "TFileIOBuffer.h"
#include "TLog.h"
#include "TPeakBuilder.h"
#include "TProject.h"
#include "TProjectManager.h"
#include "TResourcesManager.h"

#include <algorithm>

TWaveform::TWaveform(TBufferedAudioStreamReader* source)
    : m_source(resources_manager()->get_readsource(source->get_id()))
    , m_decodeBuffer(std::make_unique<TFileIOBuffer>())
{
    // Next to the project's audio sources, like the old peak files
    QString path;
    if (TProject* project = pm().get_project()) {
        path = project->get_root_dir() + "/peakfiles/";
    } else {
        path = source->get_dir();
        path.replace("audiosources", "peakfiles");
    }
    m_peakPath = path + source->get_name() + ".peaks";
}

TWaveform::~TWaveform()
{
    if (m_job) {
        m_job->cancel();
    }
    delete m_source;
}

int TWaveform::channel_count() const
{
    return m_source ? int(m_source->get_channel_count()) : 0;
}

TPeakFile::SourceInfo TWaveform::source_info() const
{
    return TPeakFile::SourceInfo::from_file(m_source->get_filename(), m_source->get_file_rate(),
                                            int(m_source->get_channel_count()), m_source->get_nframes());
}

TWaveform::State TWaveform::state()
{
    if (!m_opened) {
        m_opened = true;
        open_or_build();
    }
    return m_state;
}

void TWaveform::open_or_build()
{
    if (!m_source || m_source->get_error() < 0) {
        TLOG_W("peaks", "no readable source for {}", m_peakPath.toStdString());
        m_state = State::Failed;
        return;
    }

    const TPeakFile::Status status = m_file.open(m_peakPath, source_info());
    switch (status) {
    case TPeakFile::Status::Ok:
        TLOG_D("peaks", "opened {}", m_peakPath.toStdString());
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

    TLOG_I("peaks", "building {} ({})", m_peakPath.toStdString(),
           status == TPeakFile::Status::Missing ? "no peak file yet" : "the peak file does not match the source");
    ++m_builds;
    m_state = State::Building;
    m_progress = 0;
    m_job = peak_builder().build(m_peakPath, m_source->get_id());
    if (!m_job) {
        TLOG_W("peaks", "no reader for the source of {}", m_peakPath.toStdString());
        m_state = State::Failed;
        return;
    }
    connect(m_job.get(), &TPeakBuildJob::progress, this, [this](int percent) {
        m_progress = percent;
        emit progress(percent);
    });
    connect(m_job.get(), &TPeakBuildJob::finished, this, &TWaveform::build_finished);
}

void TWaveform::build_finished()
{
    const bool succeeded = m_job && m_job->succeeded();
    m_job.reset();

    if (succeeded) {
        open_or_build();
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
    return TWaveformColumns::query(m_file.levels(), this, start, columnWidth, count, out);
}

float TWaveform::max_amplitude(const TTimeRef& start, const TTimeRef& end)
{
    if (state() != State::Ready) {
        return -1.0f;
    }
    const uint rate = m_source->get_file_rate();
    return TWaveformColumns::max_amplitude(m_file.levels(), this,
                                           TTimeRef::frame_at_or_after(start, rate), TTimeRef::frame_at_or_after(end, rate));
}

qint64 TWaveform::read(qint64 firstFrame, qint64 count, float* const* channels)
{
    const uint channelCount = m_source->get_channel_count();
    m_decodeBuffer->check_capacity(uint(count), channelCount);

    const int read = m_source->file_read(*m_decodeBuffer, nframes_t(firstFrame), nframes_t(count));
    if (read <= 0) {
        return 0;
    }

    const qint64 frames = std::min<qint64>(read, count);
    for (uint c = 0; c < channelCount; ++c) {
        std::copy_n(m_decodeBuffer->get_channel_buffer(c).get_data(nframes_t(frames)), frames, channels[c]);
    }
    return frames;
}
