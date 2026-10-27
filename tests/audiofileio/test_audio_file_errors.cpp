// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of why the audio file library says a call failed
// (doc/design/audiofileio-errors.md): opening a reader tells a missing file
// (NotFound) from one that cannot be read (CannotOpen: no permission, an
// unknown format, a broken scenario); a writer without an input rate or a
// directory does not open; a second close() is no error, a write after it
// is.

#include "TAudioReader.h"
#include "TAudioWriter.h"
#include "TFileIOBuffer.h"

#include <QFile>
#include <QTemporaryDir>

#include <cstdio>
#include <unistd.h>

static int failures = 0;

static void check(bool ok, const char* what)
{
    std::printf("%s: %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) {
        ++failures;
    }
}

static QString write_file(const QTemporaryDir& dir, const char* name, const QByteArray& content)
{
    const QString path = dir.filePath(QLatin1String(name));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(content) != content.size()) {
        return QString();
    }
    return path;
}

static bool fails_with(const std::expected<std::unique_ptr<TAudioReader>, TAudioFileError>& reader, TAudioFileError::Code code)
{
    return !reader && reader.error().code == code && !reader.error().detail.isEmpty();
}

static void test_open_reader(const QTemporaryDir& dir)
{
    check(fails_with(TAudioReader::open(dir.filePath(QStringLiteral("not-there.wav"))), TAudioFileError::Code::NotFound),
          "a missing file: NotFound");

    const QString text = write_file(dir, "text.wav", QByteArray("this is no audio, only some text in a file named .wav"));
    const auto unknown = TAudioReader::open(text);
    check(fails_with(unknown, TAudioFileError::Code::CannotOpen) && unknown.error().detail.contains(QLatin1String("no decoder")),
          "a file of no known format: CannotOpen, no decoder");

    const QString broken = write_file(dir, "broken.tscenario", QByteArray("{\"version\": 1,"));
    const auto scenario = TAudioReader::open(broken);
    check(fails_with(scenario, TAudioFileError::Code::CannotOpen) && scenario.error().detail.contains(QLatin1String("not a scenario")),
          "a broken scenario: CannotOpen, with why");

    // root reads any file: no permission test then
    if (geteuid() != 0) {
        const QString locked = write_file(dir, "locked.wav", QByteArray("RIFF"));
        QFile::setPermissions(locked, QFileDevice::Permissions());
        check(fails_with(TAudioReader::open(locked), TAudioFileError::Code::CannotOpen), "a file without read permission: CannotOpen");
        QFile::setPermissions(locked, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }
}

static void test_writer(const QTemporaryDir& dir)
{
    TAudioEncodeSettings settings;
    settings.writerType = TraversoDAW::WriterType::SNDFILE;
    settings.fileFormat = TraversoDAW::FileFormat::WAV;
    settings.dataFormat = TraversoDAW::DataFormat::FLOAT;
    settings.channelCount = 2;
    settings.sampleRate = 48000;

    const auto noRate = TAudioWriter::open(settings, dir.filePath(QStringLiteral("norate.wav")), 0);
    check(!noRate && noRate.error().code == TAudioFileError::Code::CannotOpen, "a writer without an input rate: CannotOpen");

    const auto noDir = TAudioWriter::open(settings, dir.filePath(QStringLiteral("no/such/dir/file.wav")), 48000);
    check(!noDir && noDir.error().code == TAudioFileError::Code::CannotOpen && !noDir.error().detail.isEmpty(),
          "a writer in a directory that is not there: CannotOpen, with why");

    auto writer = TAudioWriter::open(settings, dir.filePath(QStringLiteral("closed.wav")), 48000);
    if (!writer) {
        check(false, "a writer opens");
        return;
    }
    TFileIOBuffer buffer;
    buffer.check_capacity(1000, 2);
    buffer.silence(0, 1000, 2);
    const TAudioFileResult written = (*writer)->write(buffer, 1000);
    check(written.frames == 1000 && !written.error, "a write gives its frames and no error");
    check((*writer)->close().has_value(), "close() completes the file");
    check((*writer)->close().has_value(), "a second close() does nothing and is no error");
    const TAudioFileResult late = (*writer)->write(buffer, 1000);
    check(late.frames == 0 && late.error && late.error->code == TAudioFileError::Code::WriteFailed, "a write after close(): WriteFailed");
}

int main()
{
    QTemporaryDir dir;
    if (!dir.isValid()) {
        std::printf("no temporary directory\n");
        return 1;
    }

    test_open_reader(dir);
    test_writer(dir);

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all audio file error tests passed\n");
    return 0;
}
