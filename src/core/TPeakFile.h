// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "TPeakPyramid.h"

#include <QFile>
#include <QString>

/**
 * @brief Peak file format 2: a TPeakLevels pyramid on disk, read mapped.
 *
 * The file is a cache of its source: when it is missing, from another
 * version, or does not match the source (rate, channels, frames, file size,
 * modification time), open() says so and the caller rebuilds it. Written with
 * QSaveFile, so a crash never leaves half a file. An open peak file holds a
 * mapping, not a file descriptor. Layout: see
 * doc/design/peak-redesign.md.
 */
class TPeakFile
{
public:
    // What the file must match
    struct SourceInfo
    {
        uint rate{0};
        int channelCount{0};
        qint64 frameCount{0};
        qint64 fileSize{0};
        qint64 modified{0};   // ms since epoch, UTC

        // rate, channels and frames from the reader, size and time from the file
        static SourceInfo from_file(const QString& sourcePath, uint rate, int channelCount, qint64 frameCount);
    };

    enum class Status
    {
        Ok,
        Missing,    // no file: build it
        Invalid,    // other version, other source or damaged: build it again
        Error       // the file system refused (permissions, mapping)
    };

    TPeakFile() = default;
    ~TPeakFile();
    TPeakFile(const TPeakFile&) = delete;
    TPeakFile& operator=(const TPeakFile&) = delete;

    // levels must be complete (TPeakPyramidBuilder::finish())
    static Status write(const QString& path, const TPeakLevels& levels, const SourceInfo& source, QString* error = nullptr);

    Status open(const QString& path, const SourceInfo& expected);
    void close();

    bool is_open() const { return m_map != nullptr; }
    // Valid while the file is open
    const TPeakLevels& levels() const { return m_levels; }

    static constexpr quint32 FormatVersion = 2;

private:
    Status validate(const SourceInfo& expected, qint64 fileSize);

    QFile m_file;
    uchar* m_map{nullptr};
    TPeakLevels m_levels;
};
