// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "TPeakBuildService.h"
#include "TPeakFile.h"
#include "TTimeRef.h"
#include "TWaveformColumns.h"

#include <QFutureWatcher>
#include <QObject>
#include <QTimer>
#include <QString>

#include <functional>
#include <memory>

class TBufferedAudioStream;
class TBufferedAudioStreamReader;
class TPeakRecording;
class TFileIOBuffer;

/**
 * @brief The waveform of one audio source for its views.
 *
 * Opens the source's peak file (TPeakFile), has a file found on disk checked
 * against its checksums once and a missing or stale one built (through the
 * PeakJobs it was given: the sheet's DiskIO does both in its idle time), and
 * answers column queries (TWaveformColumns) from the pyramid or from the
 * source's samples with a reader of its own. GUI thread only.
 */
class TWaveform : public QObject, public TWaveformSampleSource
{
    Q_OBJECT

public:
    enum class State
    {
        Building,
        Ready,
        Failed
    };

    // source: the clip's reader. The waveform reads samples with a reader
    // of its own, opened on the first sample query and closed again when
    // unused for a while, so a clip normally holds one open file.
    // How the peak file gets built or checked: starts or joins the job
    struct PeakJobs
    {
        std::function<QFuture<TPeakBuildResult>(const QString& audioPath, const QString& peakPath)> build;
        std::function<QFuture<TPeakBuildResult>(const QString& peakPath, const TPeakFile::SourceInfo& expected)> verify;
    };
    TWaveform(TBufferedAudioStreamReader* source, PeakJobs jobs);
    // A recording in progress: painted from its peaks as they are recorded
    // (the written part from the peak file), no samples
    explicit TWaveform(std::shared_ptr<TPeakRecording> live);
    // Where the peak file of an audio source lives
    static QString peak_path(const TBufferedAudioStream* source);
    ~TWaveform();

    // How far the waveform of a recording in progress reaches: whole peak
    // blocks published by the builder (the clip's length runs ahead of it)
    TTimeRef recorded_length() const;

    // Opens the peak file, or starts building it when it is missing or
    // stale. Called when the clip has its source and its sheet, so a peak
    // file never waits for its clip to become visible.
    void prepare();
    State state();
    int progress() const { return m_progress; }
    int channel_count() const;

    // Columns for the source time range starting at start, see
    // TWaveformColumns::query(). Source::None while not Ready.
    TWaveformColumns::Source columns(const TTimeRef& start, const TTimeRef& columnWidth, int count, TWaveformColumn* out);

    // The highest |sample| of all channels between start and end (source
    // time), exact; -1 while not Ready or while the peak file is checked:
    // a damaged file must not set a clip's gain
    float max_amplitude(const TTimeRef& start, const TTimeRef& end);

    // The zero crossing of the source nearest to time, at most maxDistance
    // away; an invalid TTimeRef when there is none (or while not Ready)
    TTimeRef nearest_zero_crossing(const TTimeRef& time, const TTimeRef& maxDistance);

    // TWaveformSampleSource
    qint64 read(qint64 firstFrame, qint64 count, float* const* channels) override;

signals:
    void progress(int percent);
    // The waveform changed: a build finished, or a damaged file is built again
    void ready();

private:
    TPeakFile::SourceInfo source_info() const;
    void open_or_build();
    void build_finished();
    void start_build(const char* reason);
    TBufferedAudioStreamReader* reader();

    qint64 m_sourceId{0};
    QString m_fileName;
    uint m_rate{0};
    int m_channelCount{0};
    qint64 m_frameCount{0};
    bool m_sourceValid{false};
    TBufferedAudioStreamReader* m_source{nullptr};   // only while samples are read
    QTimer m_closeReader;
    std::shared_ptr<TPeakRecording> m_live;
    QString m_peakPath;
    TPeakFile m_file;
    std::unique_ptr<TFileIOBuffer> m_decodeBuffer;
    PeakJobs m_jobs;
    bool m_verifying{false};
    QFutureWatcher<TPeakBuildResult> m_build;
    State m_state{State::Building};
    bool m_opened{false};
    int m_progress{0};
    int m_builds{0};
    int m_cancelledBuilds{0};
};
