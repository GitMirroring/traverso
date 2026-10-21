// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QSet>
#include <QString>

/**
 * @brief Where peak files live: one cache directory per user, with a size
 * limit. When the cache is over its limit, the least recently used peak
 * files go; a peak file that went is built again when it is needed, it is a
 * cache (doc/design/peak-diskio.md, "The peak cache").
 *
 * The file of an audio file is named after a hash of the audio file's
 * canonical path, so two projects with a "take1.wav" each have their own
 * peak file and the same source in two projects has one. The header still
 * says which source a peak file was made from (size and time), so a peak
 * file of a file that was replaced is rebuilt.
 *
 * Use: a peak file's mtime is its last use (touch(); atime is useless, many
 * file systems are mounted noatime or relatime). The files this process has
 * open are registered (opened()/closed(), counted: a file can be open more
 * than once); evict() leaves them, and anything younger than an hour, alone.
 *
 * Thread-safe: the settings are set on the GUI thread, the eviction runs on
 * a pool thread, files are opened on several threads.
 */
class TPeakCache
{
public:
    // The cache directory, created when missing: the one set, or the
    // platform's cache location plus "/peaks" (Linux
    // ~/.cache/Traverso/Traverso/peaks, macOS
    // ~/Library/Caches/Traverso/Traverso/peaks, Windows
    // %LOCALAPPDATA%/Traverso/Traverso/cache/peaks)
    static QString directory();
    // From the setting Peaks/cachedir; empty: the platform's cache location.
    // Tests set a directory of their own.
    static void set_directory(const QString& directory);
    // Bytes; 0: no limit (from the setting Peaks/cachelimit, in MB)
    static void set_limit(qint64 bytes);
    static qint64 limit();

    // Where the peak file of the audio file sourcePath lives. The audio file
    // need not exist yet (a recording), its directory must. Empty for a path
    // without a file name.
    static QString path_for(const QString& sourcePath);

    // Marks a peak file as used now. A cache that cannot be written (read
    // only) is no error.
    static void touch(const QString& peakPath);

    // A peak file this process has open (or is writing): evict() leaves it
    static void opened(const QString& peakPath);
    static void closed(const QString& peakPath);
    // The file names of the peak files open now
    static QSet<QString> open_files();

    // Removes the least recently used peak files until the cache is within
    // its limit. Leaves the files named in inUse, peak files younger than
    // an hour (just built, not opened yet: removed, its waveform would fail
    // for the session), files that changed since the scan, and part files
    // younger than a day (a build or recording going on, maybe in another
    // Traverso); older part files are left from a crash and go. A file that cannot be
    // removed (Windows: mapped by another Traverso) is skipped. On Linux
    // and macOS a file another Traverso has mapped can go: its mapping stays
    // valid until that one closes it. Limit 0: nothing goes.
    static void evict(const QSet<QString>& inUse);
    // evict(open_files()) on the global thread pool, unless one runs
    static void evict_later();
};
