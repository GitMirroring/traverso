// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Measurements of the peak files on real audio, not part of ctest. Build it
// in Release. Written against the API that peak format 5 and 6 share, so the
// same file measures both (doc/design/peaks-v6-report.md).
//
//   measure_peaks build <audio> <peaks>    build the peak file: time, size
//   measure_peaks max <audio> <peaks>      max_amplitude over the whole source
//   measure_peaks query <audio> <peaks>    2000 columns at three zooms
//   measure_peaks record <dir>             8 mono tracks, 10 minutes: memory

#include "ResampleAudioReader.h"
#include "TFileIOBuffer.h"
#include "TPeakBuildTask.h"
#include "TPeakFile.h"
#include "TPeakPyramid.h"
#include "TPeakStream.h"
#include "TWaveformColumns.h"

#include <QElapsedTimer>
#include <QFileInfo>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <random>
#include <vector>

// Random access to the samples of an audio file, as TWaveform reads them
class FileSamples final : public TWaveformSampleSource
{
public:
    explicit FileSamples(const QString& fileName) : m_reader(fileName) {}

    bool is_valid() { return m_reader.is_valid(); }
    uint rate() { return m_reader.get_file_rate(); }
    int channel_count() { return int(m_reader.get_num_channels()); }
    qint64 frame_count() { return qint64(m_reader.get_nframes()); }

    qint64 read(qint64 firstFrame, qint64 count, float* const* channels) override
    {
        m_buffer.check_capacity(nframes_t(count), uint(channel_count()));
        const qint64 read = std::clamp<qint64>(qint64(m_reader.read_from(m_buffer, nframes_t(firstFrame), nframes_t(count))), 0, count);
        for (int c = 0; c < channel_count(); ++c) {
            std::copy_n(m_buffer.get_channel_buffer(uint(c)).get_data(nframes_t(std::max<qint64>(read, 1))), read, channels[c]);
        }
        frames += std::max<qint64>(read, 0);
        ++calls;
        return read;
    }

    qint64 frames{0};
    qint64 calls{0};

private:
    TResampleAudioReader m_reader;
    TFileIOBuffer m_buffer;
};

static int build(const QString& audio, const QString& peaks)
{
    QFile::remove(peaks);
    TResampleAudioReader reader(audio);
    TPeakBuildTask task(TPeakBuildTask::audio_file(audio), audio,
                        std::make_shared<TPeakStream>(reader.get_file_rate(), int(reader.get_num_channels()), peaks));
    QElapsedTimer timer;
    timer.start();
    while (task.step()) {
    }
    const qint64 stepped = timer.elapsed();
    task.publish();
    if (task.state() != TPeakBuildTask::State::Done) {
        std::printf("build failed: %s\n", qPrintable(task.error()));
        return 1;
    }
    std::printf("format %u build %lld ms (publish %lld ms) size %lld bytes\n", TPeakFile::FormatVersion, (long long)stepped,
                (long long)(timer.elapsed() - stepped), (long long)QFileInfo(peaks).size());
    return 0;
}

static bool open_file(FileSamples& samples, TPeakFile& file, const QString& audio, const QString& peaks)
{
    if (!samples.is_valid()) {
        std::printf("cannot read %s\n", qPrintable(audio));
        return false;
    }
    const auto source = TPeakFile::SourceInfo::from_file(audio, samples.rate(), samples.channel_count(), samples.frame_count());
    if (file.open(peaks, source) != TPeakFile::Status::Ok) {
        std::printf("cannot open %s (build it first)\n", qPrintable(peaks));
        return false;
    }
    return true;
}

static int max_amplitude(const QString& audio, const QString& peaks)
{
    FileSamples samples(audio);
    TPeakFile file;
    if (!open_file(samples, file, audio, peaks)) {
        return 1;
    }
    QElapsedTimer timer;
    timer.start();
    const float amplitude = TWaveformColumns::max_amplitude(file.levels(), &samples, 0, file.levels().frame_count());
    std::printf("format %u max_amplitude %.9g in %lld ms, %lld frames read in %lld reads\n", TPeakFile::FormatVersion, amplitude,
                (long long)timer.elapsed(), (long long)samples.frames, (long long)samples.calls);
    return 0;
}

static int query(const QString& audio, const QString& peaks)
{
    FileSamples samples(audio);
    TPeakFile file;
    if (!open_file(samples, file, audio, peaks)) {
        return 1;
    }
    const TPeakLevels& levels = file.levels();
    const int count = 2000;
    std::vector<TWaveformColumn> out(size_t(count) * size_t(levels.channel_count()));
    for (const qint64 framesPerColumn : {qint64(256), qint64(8192), qint64(levels.frame_count() / count)}) {
        const TTimeRef width(uint64_t(framesPerColumn), levels.rate());
        // Twenty places, each queried once first so the mapping's pages are
        // in (a first touch costs a page fault, not what is measured)
        std::mt19937_64 random(1);
        std::vector<TTimeRef> starts;
        const qint64 span = framesPerColumn * count;
        for (int i = 0; i < 20; ++i) {
            const qint64 startFrame = span < levels.frame_count()
                ? std::uniform_int_distribution<qint64>(0, levels.frame_count() - span)(random) : 0;
            starts.push_back(TTimeRef(uint64_t(startFrame), levels.rate()));
            TWaveformColumns::query(levels, nullptr, starts.back(), width, count, out.data());
        }
        double best = 1e30;
        for (int run = 0; run < 10; ++run) {
            for (const TTimeRef& start : starts) {
                QElapsedTimer timer;
                timer.start();
                TWaveformColumns::query(levels, nullptr, start, width, count, out.data());
                best = std::min(best, double(timer.nsecsElapsed()) / 1000.0);
            }
        }
        std::printf("format %u query %d columns of %lld frames: best %.1f us\n", TPeakFile::FormatVersion, count,
                    (long long)framesPerColumn, best);
    }
    return 0;
}

static int record(const QString& dir)
{
    const uint rate = 48000;
    const int tracks = 8;
    const qint64 frames = qint64(rate) * 600;
    const qint64 buffer = 1024;
    // Bytes of one block: min/max, and from format 6 on the RMS code
    const qint64 blockBytes = qint64(sizeof(TPeakBlock)) + (TPeakFile::FormatVersion >= 6 ? 2 : 0);

    std::vector<std::unique_ptr<TPeakStream>> recordings;
    for (int t = 0; t < tracks; ++t) {
        recordings.push_back(std::make_unique<TPeakStream>(rate, 1, dir + QStringLiteral("/track-%1.peaks").arg(t)));
        if (recordings.back()->open() != TPeakFile::Status::Ok) {
            std::printf("cannot open the recording's peak file\n");
            return 1;
        }
    }
    std::vector<float> samples(static_cast<size_t>(buffer));
    std::mt19937 random(3);
    std::uniform_real_distribution<float> noise(-0.5f, 0.5f);
    qint64 peakBlocks = 0;
    QElapsedTimer timer;
    timer.start();
    for (qint64 position = 0; position < frames; position += buffer) {
        for (float& sample : samples) {
            sample = noise(random);
        }
        const float* channels[1] = {samples.data()};
        qint64 blocks = 0;
        for (auto& recording : recordings) {
            recording->append(channels, buffer);
            blocks += recording->memory_blocks();
        }
        peakBlocks = std::max(peakBlocks, blocks);
    }
    const qint64 elapsed = timer.elapsed();
    for (auto& recording : recordings) {
        recording->finish(dir + QStringLiteral("/none.wav"));
    }
    std::printf("format %u record %d mono tracks x 10 min: at most %lld blocks = %.1f KB of peaks in memory (%lld ms)\n",
                TPeakFile::FormatVersion, tracks, (long long)peakBlocks, double(peakBlocks * blockBytes) / 1024.0, (long long)elapsed);
    return 0;
}

int main(int argc, char** argv)
{
    if (argc >= 4 && std::strcmp(argv[1], "build") == 0) {
        return build(QString::fromLocal8Bit(argv[2]), QString::fromLocal8Bit(argv[3]));
    }
    if (argc >= 4 && std::strcmp(argv[1], "max") == 0) {
        return max_amplitude(QString::fromLocal8Bit(argv[2]), QString::fromLocal8Bit(argv[3]));
    }
    if (argc >= 4 && std::strcmp(argv[1], "query") == 0) {
        return query(QString::fromLocal8Bit(argv[2]), QString::fromLocal8Bit(argv[3]));
    }
    if (argc >= 3 && std::strcmp(argv[1], "record") == 0) {
        return record(QString::fromLocal8Bit(argv[2]));
    }
    std::printf("usage: measure_peaks build|max|query <audio> <peaks> | record <dir>\n");
    return 2;
}
