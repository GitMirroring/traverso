// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TPeakCache.h"

#include "TLog.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QStandardPaths>
#include <QThreadPool>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <vector>

struct TPeakCacheState
{
    QMutex mutex;
    QString directory;          // empty: the platform's cache location
    bool created{false};
    qint64 limit{qint64(10240) << 20};
    QHash<QString, int> open;   // file name: how often open
    std::atomic<bool> evicting{false};
};

static TPeakCacheState& cache_state()
{
    static TPeakCacheState state;
    return state;
}

// The file name of an audio file, kept readable for debugging: at most 40
// characters, only those every file system takes
static QString safe_name(const QString& fileName)
{
    QString name;
    for (const QChar c : fileName) {
        if (name.size() == 40) {
            break;
        }
        const char16_t u = c.unicode();
        const bool plain = (u >= u'a' && u <= u'z') || (u >= u'A' && u <= u'Z') || (u >= u'0' && u <= u'9')
                           || u == u'.' || u == u'_' || u == u'-';
        name += plain ? c : QLatin1Char('_');
    }
    return name;
}

QString TPeakCache::directory()
{
    TPeakCacheState& state = cache_state();
    QMutexLocker locker(&state.mutex);
    if (state.directory.isEmpty()) {
        state.directory = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/peaks");
    }
    if (!state.created) {
        state.created = QDir().mkpath(state.directory);
    }
    return state.directory;
}

void TPeakCache::set_directory(const QString& directory)
{
    TPeakCacheState& state = cache_state();
    QMutexLocker locker(&state.mutex);
    if (directory != state.directory) {
        state.directory = directory;
        state.created = false;
    }
}

void TPeakCache::set_limit(qint64 bytes)
{
    TPeakCacheState& state = cache_state();
    QMutexLocker locker(&state.mutex);
    state.limit = std::max<qint64>(0, bytes);
}

qint64 TPeakCache::limit()
{
    TPeakCacheState& state = cache_state();
    QMutexLocker locker(&state.mutex);
    return state.limit;
}

QString TPeakCache::path_for(const QString& sourcePath)
{
    // The key is the canonical path: a symlink and its file share it. A file
    // that does not exist yet (a recording) has none; its directory's
    // canonical path and its name give the same key it gets once it exists.
    // In NFC: macOS can hand out file names decomposed.
    const QFileInfo info(sourcePath);
    QString key = info.canonicalFilePath();
    if (key.isEmpty()) {
        const QString directory = QFileInfo(info.absolutePath()).canonicalFilePath();
        key = (directory.isEmpty() ? info.absolutePath() : directory) + QLatin1Char('/') + info.fileName();
    }
    key = key.normalized(QString::NormalizationForm_C);
    const QByteArray hash = QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256).toHex().left(32);
    // The readable part from the key too: a symlink gets its file's name
    return directory() + QLatin1Char('/') + QString::fromLatin1(hash) + QLatin1Char('-')
           + safe_name(key.mid(key.lastIndexOf(QLatin1Char('/')) + 1)) + QStringLiteral(".peaks");
}

void TPeakCache::touch(const QString& peakPath)
{
    std::error_code error;
    std::filesystem::last_write_time(QFileInfo(peakPath).filesystemFilePath(), std::filesystem::file_time_type::clock::now(), error);
    if (error) {
        TLOG_D("peaks", "cannot mark {} as used: {}", peakPath.toStdString(), error.message());
    }
}

void TPeakCache::opened(const QString& peakPath)
{
    TPeakCacheState& state = cache_state();
    QMutexLocker locker(&state.mutex);
    ++state.open[QFileInfo(peakPath).fileName()];
}

void TPeakCache::closed(const QString& peakPath)
{
    TPeakCacheState& state = cache_state();
    QMutexLocker locker(&state.mutex);
    const QString name = QFileInfo(peakPath).fileName();
    const auto it = state.open.find(name);
    if (it != state.open.end() && --it.value() <= 0) {
        state.open.erase(it);
    }
}

QSet<QString> TPeakCache::open_files()
{
    TPeakCacheState& state = cache_state();
    QMutexLocker locker(&state.mutex);
    QSet<QString> names;
    for (auto it = state.open.cbegin(); it != state.open.cend(); ++it) {
        names.insert(it.key());
    }
    return names;
}

void TPeakCache::evict(const QSet<QString>& inUse)
{
    const qint64 limitBytes = limit();
    if (limitBytes <= 0) {
        return;
    }

    struct Entry
    {
        std::filesystem::path path;
        QString name;
        qint64 size;
        std::filesystem::file_time_type time;
        bool part;
    };
    std::vector<Entry> entries;
    qint64 total = 0;
    std::error_code error;
    for (std::filesystem::directory_iterator it(QFileInfo(directory()).filesystemFilePath(), error), end;
         !error && it != end; it.increment(error)) {
        std::error_code entryError;
        if (!it->is_regular_file(entryError)) {
            continue;
        }
        // Names are compared as file names (all peak files are in this
        // directory): std::filesystem and Qt write separators differently
        const QString name = QString::fromStdU16String(it->path().filename().u16string());
        const bool part = name.endsWith(QStringLiteral(".part"));
        if (!part && !name.endsWith(QStringLiteral(".peaks"))) {
            continue;
        }
        const qint64 size = qint64(it->file_size(entryError));
        const std::filesystem::file_time_type time = it->last_write_time(entryError);
        if (entryError) {
            continue;
        }
        entries.push_back(Entry{it->path(), name, size, time, part});
        total += size;
    }

    const std::filesystem::file_time_type now = std::filesystem::file_time_type::clock::now();
    int removed = 0;
    qint64 removedBytes = 0;
    auto remove = [&](const Entry& entry) {
        std::error_code removeError;
        if (std::filesystem::remove(entry.path, removeError) && !removeError) {
            total -= entry.size;
            ++removed;
            removedBytes += entry.size;
        }
    };

    // Part files a day old are left from a crash: they go anyway
    for (const Entry& entry : entries) {
        if (entry.part && now - entry.time > std::chrono::hours(24)) {
            remove(entry);
        }
    }

    // The least recently used first
    std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.time < b.time; });
    for (const Entry& entry : entries) {
        if (total <= limitBytes) {
            break;
        }
        if (entry.part || inUse.contains(entry.name) || now - entry.time < std::chrono::hours(1)) {
            continue;
        }
        remove(entry);
    }

    if (removed > 0) {
        TLOG_I("peaks", "cache: removed {} files ({} MB), {} MB left", removed, removedBytes >> 20, total >> 20);
    }
}

void TPeakCache::evict_later()
{
    if (cache_state().evicting.exchange(true)) {
        return;
    }
    QThreadPool::globalInstance()->start([] {
        evict(open_files());
        cache_state().evicting.store(false);
    });
}
