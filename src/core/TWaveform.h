// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "TPeakFile.h"
#include "TTimeRef.h"
#include "TWaveformColumns.h"

#include <QObject>
#include <QTimer>
#include <QString>

#include <memory>

class TBufferedAudioStream;
class TBufferedAudioStreamReader;
class TPeakPyramidBuilder;
class TFileIOBuffer;
class TPeakBuildJob;

/**
 * @brief The waveform of one audio source for its views.
 *
 * Opens the source's peak file (TPeakFile), has it built when it is missing
 * or stale, and answers column queries (TWaveformColumns) from the pyramid or
 * from the source's samples with a reader of its own. GUI thread only.
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
    explicit TWaveform(TBufferedAudioStreamReader* source);
    // A recording in progress: painted from the growing pyramid, no samples
    explicit TWaveform(std::shared_ptr<TPeakPyramidBuilder> live);
    // Where the peak file of an audio source lives
    static QString peak_path(const TBufferedAudioStream* source);
    ~TWaveform();

    // A recording that just finished: its pyramid serves the columns until
    // the peak file written from it opens
    void use_until_file_ready(std::shared_ptr<TPeakPyramidBuilder> pyramid);

    // Starts the build when the peak file is missing or stale
    State state();
    int progress() const { return m_progress; }
    int channel_count() const;

    // Columns for the source time range starting at start, see
    // TWaveformColumns::query(). Source::None while not Ready.
    TWaveformColumns::Source columns(const TTimeRef& start, const TTimeRef& columnWidth, int count, TWaveformColumn* out);

    // The highest |sample| of all channels between start and end (source
    // time), exact; -1 while not Ready
    float max_amplitude(const TTimeRef& start, const TTimeRef& end);

    // TWaveformSampleSource
    qint64 read(qint64 firstFrame, qint64 count, float* const* channels) override;

signals:
    void progress(int percent);
    void ready();

private:
    TPeakFile::SourceInfo source_info() const;
    void open_or_build();
    void build_finished();
    TBufferedAudioStreamReader* reader();

    qint64 m_sourceId{0};
    QString m_fileName;
    uint m_rate{0};
    int m_channelCount{0};
    qint64 m_frameCount{0};
    bool m_sourceValid{false};
    TBufferedAudioStreamReader* m_source{nullptr};   // only while samples are read
    QTimer m_closeReader;
    std::shared_ptr<TPeakPyramidBuilder> m_live;
    QString m_peakPath;
    TPeakFile m_file;
    std::unique_ptr<TFileIOBuffer> m_decodeBuffer;
    std::shared_ptr<TPeakBuildJob> m_job;
    State m_state{State::Building};
    bool m_opened{false};
    int m_progress{0};
    int m_builds{0};
};
