// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TPeakStream.h"

#include "TPeakCache.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <limits>

// Level-0 blocks per post-processing step: a fraction of a millisecond of
// work, and a read of 192 KB of a stereo file (mostly from the page cache)
static constexpr qint64 PostProcessBlocks = qint64(1) << 14;
// Records of a level collected before they are written (192 KB of stereo)
static constexpr qint64 FlushRecords = qint64(1) << 14;

// Post-processing: every level above level 0, made from level 0 in one pass
struct TPeakStream::PostProcess
{
    struct Level
    {
        qint64 count{0};
        qint64 offset{0};               // where its records start in the file
        // A block waiting for its pair: min/max codes, mean squares, frames
        std::vector<qint16> pending;
        std::vector<double> pendingSquares;
        qint64 pendingFrames{0};
        bool hasPending{false};
        std::vector<qint16> out;        // records not written yet
        qint64 written{0};
    };
    std::vector<Level> levels;
    qint64 next{0};                     // the next level-0 block to read
    std::vector<qint16> input;
    std::vector<double> squares;
    bool written{false};                // the file is complete, its name is next
    QElapsedTimer renaming;
};

TPeakStream::TPeakStream(uint rate, int channelCount, QString peakPath)
    : m_rate(rate)
    , m_channelCount(channelCount)
    , m_peakPath(std::move(peakPath))
    , m_min(size_t(channelCount), std::numeric_limits<float>::max())
    , m_max(size_t(channelCount), std::numeric_limits<float>::lowest())
    , m_sumSquares(size_t(channelCount), 0.0f)
{
    Q_ASSERT(rate > 0 && channelCount > 0);
    // In use as long as it lives: the cache's eviction leaves it alone. (A
    // peak file younger than an hour stays anyway.)
    TPeakCache::opened(m_peakPath);
}

TPeakStream::~TPeakStream()
{
    // A part file that never got its name (a build whose project went
    // first, a recording that was not post-processed) is of no use to anyone
    m_file.close();
    if (!published() && !m_partPath.isEmpty()) {
        QFile::remove(m_partPath);
    }
    TPeakCache::closed(m_peakPath);
}

bool TPeakStream::fail(const QString& error)
{
    m_failed = true;
    m_error = error;
    m_file.close();
    return false;
}

void TPeakStream::abandon()
{
    m_file.close();
    m_post.reset();
    if (!published() && !m_partPath.isEmpty()) {
        QFile::remove(m_partPath);
    }
    m_failed = true;
}

TPeakFile::Status TPeakStream::open(QString* error)
{
    if (!QDir().mkpath(QFileInfo(m_peakPath).absolutePath())) {
        fail(QStringLiteral("cannot create the peak file directory"));
    } else {
        // Per process and per writer, and never one that is there already:
        // two writers of the same peak file (two Traverso instances, or an
        // old and a new build of a project that was opened again) never
        // write into each other's part file, and on a cache shared by two
        // machines a process id can come twice
        static std::atomic<quint64> writers{0};
        bool opened = false;
        for (int attempt = 0; attempt < 100 && !opened; ++attempt) {
            m_partPath = m_peakPath + QStringLiteral(".%1-%2.part").arg(QCoreApplication::applicationPid()).arg(writers.fetch_add(1) + 1);
            m_file.setFileName(m_partPath);
            opened = m_file.open(QIODevice::WriteOnly | QIODevice::NewOnly);
            if (!opened && !QFile::exists(m_partPath)) {
                break;   // not because it exists: no other name helps
            }
        }
        // The header comes last: zeros until then, so an unfinished file is Invalid
        const QByteArray zeros(int(TPeakFile::HeaderBytes), '\0');
        if (!opened) {
            m_partPath.clear();
            fail(m_file.errorString());
        } else if (m_file.write(zeros) != zeros.size()) {
            fail(m_file.errorString());
        }
    }
    if (m_failed && error) {
        *error = m_error;
    }
    return m_failed ? TPeakFile::Status::Error : TPeakFile::Status::Ok;
}

void TPeakStream::append(const float* const* channels, qint64 frames)
{
    if (m_failed) {
        return;
    }

    qint64 position = 0;
    while (position < frames) {
        const int count = int(std::min<qint64>(frames - position, TPeakLevels::BlockFrames - m_blockFill));
        for (int c = 0; c < m_channelCount; ++c) {
            const float* samples = channels[c] + position;
            float low = m_min[size_t(c)];
            float high = m_max[size_t(c)];
            float squares = m_sumSquares[size_t(c)];
            for (int i = 0; i < count; ++i) {
                low = std::min(low, samples[i]);
                high = std::max(high, samples[i]);
                squares += samples[i] * samples[i];
            }
            m_min[size_t(c)] = low;
            m_max[size_t(c)] = high;
            m_sumSquares[size_t(c)] = squares;
        }
        position += count;
        m_blockFill += count;
        if (m_blockFill == TPeakLevels::BlockFrames) {
            close_block();
        }
    }
    m_frames += frames;
    write_records();
}

void TPeakStream::close_block()
{
    if (m_blockFill == 0) {
        return;
    }
    // Min and max of every channel, then the RMS of every channel; a partial
    // last block weighs its own frames
    for (int c = 0; c < m_channelCount; ++c) {
        m_records.push_back(TPeakLevels::code_at_or_below(m_min[size_t(c)]));
        m_records.push_back(TPeakLevels::code_at_or_above(m_max[size_t(c)]));
    }
    for (int c = 0; c < m_channelCount; ++c) {
        double meanSquare = double(m_sumSquares[size_t(c)]) / double(m_blockFill);
        if (!std::isfinite(meanSquare)) {
            // A NaN, an inf or a square too large for a float among the
            // samples: this block gets its peak (min and max leave NaN out),
            // clamped to the code range, so the blocks above, made from it,
            // stay finite. One broken sample (a plugin, a float file) does not
            // take the RMS of the whole source along.
            const float low = m_min[size_t(c)];
            const float high = m_max[size_t(c)];
            const float peak = low <= high ? std::max(std::fabs(low), std::fabs(high)) : 0.0f;
            const double bounded = std::min(peak, TPeakLevels::max_value());
            meanSquare = bounded * bounded;
        }
        m_records.push_back(TPeakLevels::code_nearest(float(std::sqrt(meanSquare))));
        m_min[size_t(c)] = std::numeric_limits<float>::max();
        m_max[size_t(c)] = std::numeric_limits<float>::lowest();
        m_sumSquares[size_t(c)] = 0.0f;
    }
    m_blockFill = 0;
}

bool TPeakStream::write_records()
{
    if (m_records.empty()) {
        return true;
    }
    // Out of QFile's buffer too: a reader reads the file
    const qint64 bytes = qint64(m_records.size() * sizeof(qint16));
    if (m_file.write(reinterpret_cast<const char*>(m_records.data()), bytes) != bytes || !m_file.flush()) {
        return fail(m_file.errorString());   // nothing written from here on
    }
    m_records.clear();
    m_writtenFrames.store(m_frames - m_blockFill, std::memory_order_release);
    return true;
}

TPeakFile::Status TPeakStream::finish(const QString& audioPath, QString* error)
{
    if (!m_failed) {
        close_block();
        if (write_records() && m_frames == 0) {
            fail(QStringLiteral("no audio"));
        }
    }
    if (m_failed) {
        // No peak file: the waveform builds it from the audio
        abandon();
        if (error) {
            *error = m_error;
        }
        return TPeakFile::Status::Error;
    }
    m_file.close();
    m_source = TPeakFile::SourceInfo::from_file(audioPath, m_rate, m_channelCount, m_frames);
    return TPeakFile::Status::Ok;
}

bool TPeakStream::post_process()
{
    if (m_failed || published()) {
        return false;
    }
    auto failed = [this](const QString& error) {
        fail(error);
        abandon();
        return false;
    };
    const int codes = TPeakLevels::record_codes(m_channelCount);
    const qint64 recordBytes = TPeakFile::record_bytes(m_channelCount);

    if (!m_post) {
        m_file.setFileName(m_partPath);
        if (!m_file.open(QIODevice::ReadWrite)) {
            return failed(m_file.errorString());
        }
        m_post = std::make_unique<PostProcess>();
        m_post->squares.resize(size_t(m_channelCount));
        qint64 offset = TPeakFile::HeaderBytes;
        for (const qint64 count : TPeakFile::block_counts(m_frames)) {
            PostProcess::Level level;
            level.count = count;
            level.offset = offset;
            level.pending.resize(size_t(2 * m_channelCount));
            level.pendingSquares.resize(size_t(m_channelCount));
            m_post->levels.push_back(std::move(level));
            offset += count * recordBytes;
        }
    }
    PostProcess& post = *m_post;

    const qint64 blocks = post.levels.front().count;
    if (post.next < blocks) {
        const qint64 count = std::min(PostProcessBlocks, blocks - post.next);
        post.input.resize(size_t(count * codes));
        const qint64 bytes = count * recordBytes;
        if (!m_file.seek(TPeakFile::HeaderBytes + post.next * recordBytes)
                || m_file.read(reinterpret_cast<char*>(post.input.data()), bytes) != bytes) {
            return failed(QStringLiteral("cannot read level 0: %1").arg(m_file.errorString()));
        }
        for (qint64 i = 0; i < count; ++i) {
            // The RMS codes decoded: the levels above add their squares
            const qint16* record = post.input.data() + i * codes;
            for (int c = 0; c < m_channelCount; ++c) {
                const double rms = TPeakLevels::magnitude_value(std::max(int(record[2 * m_channelCount + c]), 0));
                post.squares[size_t(c)] = rms * rms;
            }
            const qint64 first = (post.next + i) << TPeakLevels::BlockFramesShift;
            if (!add_block(0, record, post.squares.data(), std::min<qint64>(TPeakLevels::BlockFrames, m_frames - first))) {
                return failed(m_error);
            }
        }
        post.next += count;
        return true;
    }

    if (!post.written) {
        // A level with an odd number of blocks has a last block without a
        // pair: it covers the end of the source alone, so it moves up as it is
        for (int level = 0; level + 1 < int(post.levels.size()); ++level) {
            PostProcess::Level& l = post.levels[size_t(level)];
            if (l.hasPending) {
                l.hasPending = false;
                if (!add_block(level + 1, l.pending.data(), l.pendingSquares.data(), l.pendingFrames)) {
                    return failed(m_error);
                }
            }
        }
        for (int level = 1; level < int(post.levels.size()); ++level) {
            if (!write_level(level)) {
                return failed(m_error);
            }
            Q_ASSERT(post.levels[size_t(level)].written == post.levels[size_t(level)].count);
        }

        // The header last: the file is complete now
        const QByteArray header = TPeakFile::header(m_source);
        if (!m_file.seek(0) || m_file.write(header) != header.size() || !m_file.flush()) {
            return failed(m_file.errorString());
        }
        m_file.close();
        post.written = true;
    }

    // Its name. Windows renames no file that is open: the waveform of a
    // recording opens it for a moment per repaint, a virus scanner or the
    // indexer may too. A refused rename is tried again in the next steps
    // (not in a sleep: the step is part of a DiskIO round), for a while.
    QString error;
    if (TPeakFile::publish(m_partPath, m_peakPath, &error) != TPeakFile::Status::Ok) {
#if defined(Q_OS_WIN)
        if (!post.renaming.isValid()) {
            post.renaming.start();
        }
        if (post.renaming.elapsed() < 2000) {
            return true;
        }
#endif
        return failed(QStringLiteral("cannot rename the part file: %1").arg(error));
    }
    m_post.reset();
    m_published.store(true, std::memory_order_release);
    return false;
}

bool TPeakStream::add_block(int level, const qint16* minMax, const double* meanSquares, qint64 frames)
{
    PostProcess::Level& l = m_post->levels[size_t(level)];
    if (level > 0) {
        // Every stored RMS is rounded once, from the mean squares of the
        // decoded level-0 codes
        l.out.insert(l.out.end(), minMax, minMax + 2 * m_channelCount);
        for (int c = 0; c < m_channelCount; ++c) {
            l.out.push_back(TPeakLevels::code_nearest(float(std::sqrt(meanSquares[c]))));
        }
        if (qint64(l.out.size()) >= FlushRecords * TPeakLevels::record_codes(m_channelCount) && !write_level(level)) {
            return false;
        }
    }
    if (level + 1 == int(m_post->levels.size())) {
        return true;
    }
    if (!l.hasPending) {
        std::copy_n(minMax, 2 * m_channelCount, l.pending.begin());
        std::copy_n(meanSquares, m_channelCount, l.pendingSquares.begin());
        l.pendingFrames = frames;
        l.hasPending = true;
        return true;
    }

    // The pair makes the block above it
    l.hasPending = false;
    const qint64 pairFrames = l.pendingFrames + frames;
    for (int c = 0; c < m_channelCount; ++c) {
        l.pending[size_t(2 * c)] = std::min(l.pending[size_t(2 * c)], minMax[2 * c]);
        l.pending[size_t(2 * c + 1)] = std::max(l.pending[size_t(2 * c + 1)], minMax[2 * c + 1]);
        l.pendingSquares[size_t(c)] = (l.pendingSquares[size_t(c)] * double(l.pendingFrames) + meanSquares[c] * double(frames))
                                      / double(pairFrames);
    }
    return add_block(level + 1, l.pending.data(), l.pendingSquares.data(), pairFrames);
}

bool TPeakStream::write_level(int level)
{
    PostProcess::Level& l = m_post->levels[size_t(level)];
    if (l.out.empty()) {
        return true;
    }
    const qint64 bytes = qint64(l.out.size() * sizeof(qint16));
    if (!m_file.seek(l.offset + l.written * TPeakFile::record_bytes(m_channelCount))
            || m_file.write(reinterpret_cast<const char*>(l.out.data()), bytes) != bytes) {
        return fail(m_file.errorString());
    }
    l.written += qint64(l.out.size()) / TPeakLevels::record_codes(m_channelCount);
    l.out.clear();
    return true;
}

TPeakLevels TPeakStream::read(qint64 first, qint64 last, std::vector<qint16>& records) const
{
    const qint64 frames = written_frames();
    const qint64 blocks = (frames + TPeakLevels::BlockFrames - 1) >> TPeakLevels::BlockFramesShift;
    first = std::clamp<qint64>(first, 0, blocks);
    last = std::clamp(last, first, blocks);
    const qint64 recordBytes = TPeakFile::record_bytes(m_channelCount);
    records.resize(size_t((last - first) * TPeakLevels::record_codes(m_channelCount)));

    if (last > first) {
        // Opened per read, so no handle stays open that keeps Windows from
        // renaming the file. Once renamed, from its name: level 0 is in the
        // same place.
        QFile file(m_partPath);
        bool opened = file.open(QIODevice::ReadOnly);
        if (!opened && published()) {
            file.setFileName(m_peakPath);
            opened = file.open(QIODevice::ReadOnly);
        }
        const qint64 bytes = (last - first) * recordBytes;
        const qint64 read = opened && file.seek(TPeakFile::HeaderBytes + first * recordBytes)
            ? file.read(reinterpret_cast<char*>(records.data()), bytes) : 0;
        last = first + std::max<qint64>(read, 0) / recordBytes;
    }
    return TPeakLevels(m_rate, m_channelCount, frames, {TPeakLevels::Level{records.data(), first, last}});
}
