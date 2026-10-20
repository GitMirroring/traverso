// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of TPeakCache: the key of a peak file, marking use, the
// files in use, the eviction; and what the cache relies on in TPeakFile: the
// format version, the tolerance for a source's time, and a recording that
// writes where a waveform looks.

#include "TPeakCache.h"
#include "TPeakFile.h"
#include "TPeakPyramid.h"
#include "TPeakStream.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QThread>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <vector>

static int s_failures = 0;

#define CHECK(condition, ...) \
    do { \
        if (!(condition)) { \
            ++s_failures; \
            std::printf("FAIL %s:%d: %s: ", __FILE__, __LINE__, #condition); \
            std::printf(__VA_ARGS__); \
            std::printf("\n"); \
        } \
    } while (0)

static void write_file(const QString& path, qint64 size)
{
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QByteArray(int(size), 'x'));
    }
}

// The mtime of path, set to age ago
static void make_old(const QString& path, std::chrono::seconds age)
{
    std::filesystem::last_write_time(QFileInfo(path).filesystemFilePath(), std::filesystem::file_time_type::clock::now() - age);
}

static std::chrono::seconds age_of(const QString& path)
{
    const auto time = std::filesystem::last_write_time(QFileInfo(path).filesystemFilePath());
    return std::chrono::duration_cast<std::chrono::seconds>(std::filesystem::file_time_type::clock::now() - time);
}

// A small peak file of the audio file audioPath, as a build writes it
static void write_peaks(const QString& peakPath, const QString& audioPath, qint64 modifiedShift = 0)
{
    TPeakPyramidBuilder builder(48000, 1, 2);
    std::vector<float> samples(5000);
    for (size_t n = 0; n < samples.size(); ++n) {
        samples[n] = float(n % 100) / 100.0f - 0.5f;
    }
    const float* channels[1] = {samples.data()};
    builder.append(channels, qint64(samples.size()));
    builder.finish();
    TPeakFile::SourceInfo source = TPeakFile::SourceInfo::from_file(audioPath, 48000, 1, qint64(samples.size()));
    source.modified += modifiedShift;
    CHECK(TPeakFile::write(peakPath, builder.levels(), source, nullptr, 2) == TPeakFile::Status::Ok, "write %s", qPrintable(peakPath));
}

int main()
{
    QTemporaryDir dir;
    CHECK(dir.isValid(), "no temporary directory");
    const QString cache = dir.filePath("cache/peaks");
    TPeakCache::set_directory(cache);
    CHECK(TPeakCache::directory() == cache && QFileInfo(cache).isDir(), "the cache directory is not made");

    // The key: the canonical path of the source
    QDir().mkpath(dir.filePath("a"));
    QDir().mkpath(dir.filePath("b"));
    const QString takeA = dir.filePath("a/take1.wav");
    const QString takeB = dir.filePath("b/take1.wav");
    write_file(takeA, 10);
    write_file(takeB, 10);
    {
        const QString peaks = TPeakCache::path_for(takeA);
        CHECK(peaks == TPeakCache::path_for(takeA), "the same path gives another peak file");
        CHECK(peaks != TPeakCache::path_for(takeB), "two take1.wav in two directories share a peak file");
        CHECK(QFileInfo(peaks).absolutePath() == cache && peaks.endsWith("-take1.wav.peaks"), "%s", qPrintable(peaks));
        CHECK(QFileInfo(peaks).fileName().size() == 32 + 1 + 9 + 6, "the name is not hash-name.peaks: %s", qPrintable(peaks));
        CHECK(TPeakCache::path_for(dir.filePath("a/../a/take1.wav")) == peaks, "a path with .. gives another peak file");
        QFile::link(takeA, dir.filePath("b/link.wav"));
        CHECK(TPeakCache::path_for(dir.filePath("b/link.wav")) == peaks, "a symlink gives another peak file than its file");
        QFile::link(dir.filePath("a"), dir.filePath("alink"));
        CHECK(TPeakCache::path_for(dir.filePath("alink/take1.wav")) == peaks, "a symlinked directory gives another peak file");
    }
    {
        // A recording: the file is not there yet, its directory is; the key
        // stays when the file appears
        const QString recording = dir.filePath("alink/take2.wav");
        const QString before = TPeakCache::path_for(recording);
        CHECK(QFileInfo(before).absolutePath() == cache && before.endsWith("-take2.wav.peaks"), "%s", qPrintable(before));
        write_file(dir.filePath("a/take2.wav"), 10);
        CHECK(TPeakCache::path_for(recording) == before && TPeakCache::path_for(dir.filePath("a/take2.wav")) == before,
              "the key changed when the recorded file appeared");
    }
    {
        // Names that file systems choke on, and long ones
        const QString odd = dir.filePath(QString::fromUtf8("a/Chor: \"één\" <*>?.wav"));
        const QString name = QFileInfo(TPeakCache::path_for(odd)).fileName();
        CHECK(name.endsWith(QStringLiteral("-Chor_____n______.wav.peaks")), "%s", qPrintable(name));
        const QString longName = QFileInfo(TPeakCache::path_for(dir.filePath("a/" + QString(100, QLatin1Char('x')) + ".wav"))).fileName();
        CHECK(longName.size() == 32 + 1 + 40 + 6, "a long name is not cut to 40: %s", qPrintable(longName));
        // Unicode forms of the same name give the same file (macOS hands
        // out decomposed names)
        const QString composed = dir.filePath(QString::fromUtf8("a/\xC3\xA9t\xC3\xA9.wav"));        // été
        const QString decomposed = dir.filePath(QString::fromUtf8("a/e\xCC\x81te\xCC\x81.wav"));    // e + accent
        CHECK(TPeakCache::path_for(composed) == TPeakCache::path_for(decomposed), "NFC and NFD give two peak files");
    }

    // Marking use: the mtime of the peak file, and the file still opens
    {
        const QString peaks = TPeakCache::path_for(takeA);
        write_peaks(peaks, takeA);
        make_old(peaks, std::chrono::hours(48));
        CHECK(age_of(peaks) > std::chrono::hours(47), "could not age the file");
        TPeakCache::touch(peaks);
        CHECK(age_of(peaks) < std::chrono::seconds(5), "touch() did not mark the file used");
        TPeakFile file;
        CHECK(file.open(peaks, TPeakFile::SourceInfo::from_file(takeA, 48000, 1, 5000)) == TPeakFile::Status::Ok,
              "a touched file does not open");
        // An open file is registered, counted
        CHECK(TPeakCache::open_files().contains(QFileInfo(peaks).fileName()), "an open peak file is not registered");
        TPeakFile second;
        CHECK(second.open(peaks, TPeakFile::SourceInfo::from_file(takeA, 48000, 1, 5000)) == TPeakFile::Status::Ok, "open twice");
        file.close();
        CHECK(TPeakCache::open_files().contains(QFileInfo(peaks).fileName()), "closing one of two unregisters the file");
        second.close();
        CHECK(!TPeakCache::open_files().contains(QFileInfo(peaks).fileName()), "a closed file stays registered");
        TPeakCache::touch(dir.filePath("none.peaks"));   // no file: no error, no crash
    }

    // Eviction
    {
        QDir(cache).removeRecursively();
        TPeakCache::set_directory(cache + "2");
        const QString root = TPeakCache::directory();
        auto file = [&](const char* name, qint64 kilobytes, std::chrono::seconds age) {
            const QString path = root + "/" + QString::fromLatin1(name);
            write_file(path, kilobytes * 1024);
            make_old(path, age);
            return path;
        };
        using std::chrono::hours;
        const QString oldest = file("1-oldest.peaks", 100, hours(100));
        const QString older = file("2-older.peaks", 100, hours(90));
        const QString old = file("3-old.peaks", 100, hours(80));
        const QString recent = file("4-recent.peaks", 100, hours(5));
        const QString fresh = file("5-fresh.peaks", 100, std::chrono::minutes(10));
        const QString youngPart = file("6-young.peaks.4242.part", 50, hours(1));
        const QString stalePart = file("7-stale.peaks.4242.part", 50, hours(48));
        const QString other = file("readme.txt", 500, hours(200));

        // Limit 0: no limit, nothing goes
        TPeakCache::set_limit(0);
        TPeakCache::evict({});
        CHECK(QFile::exists(oldest) && QFile::exists(stalePart), "limit 0 removed files");

        // 600 KB of cache files over a limit of 350 KB; the oldest is in use:
        // the stale part file and the two next oldest go, and that is enough
        TPeakCache::set_limit(350 * 1024);
        TPeakCache::evict({QStringLiteral("1-oldest.peaks")});
        CHECK(QFile::exists(oldest), "a file in use was removed");
        CHECK(!QFile::exists(stalePart), "a part file of two days was left");
        CHECK(QFile::exists(youngPart), "a part file of an hour was removed");
        CHECK(!QFile::exists(older) && !QFile::exists(old), "the least recently used files were left");
        CHECK(QFile::exists(recent), "more was removed than the limit needs");
        CHECK(QFile::exists(fresh) && QFile::exists(other), "a fresh file or a file that is not a peak file was removed");

        // Over the limit with only fresh and used files left: they stay
        TPeakCache::set_limit(1);
        TPeakCache::evict({QStringLiteral("1-oldest.peaks")});
        CHECK(QFile::exists(oldest) && QFile::exists(fresh) && QFile::exists(youngPart), "a used, fresh or young file was removed");
        CHECK(!QFile::exists(recent), "an unused file of 5 hours stayed over the limit");

        // In the background, with the files this process has open
        file("8-later.peaks", 10, hours(30));
        TPeakCache::opened(root + "/1-oldest.peaks");
        TPeakCache::evict_later();
        for (int i = 0; i < 200 && QFile::exists(root + "/8-later.peaks"); ++i) {
            QThread::msleep(5);
        }
        CHECK(!QFile::exists(root + "/8-later.peaks") && QFile::exists(oldest), "evict_later()");
        TPeakCache::closed(root + "/1-oldest.peaks");
        TPeakCache::set_limit(qint64(10240) << 20);
    }

    // The format: TRAVPEAK version 2; an older TRAVPEAK version and a legacy
    // file (TRAVPF 1.5) are Invalid and get rebuilt
    {
        TPeakCache::set_directory(cache);
        const QString peaks = TPeakCache::path_for(takeB);
        write_peaks(peaks, takeB);
        const auto source = TPeakFile::SourceInfo::from_file(takeB, 48000, 1, 5000);
        TPeakFile file;
        CHECK(TPeakFile::FormatVersion == 2, "format version %u", TPeakFile::FormatVersion);
        CHECK(file.open(peaks, source) == TPeakFile::Status::Ok, "a version 2 file does not open");
        file.close();
        QFile patch(peaks);
        CHECK(patch.open(QIODevice::ReadWrite) && patch.seek(8), "patch");
        const quint32 six = 6;
        patch.write(reinterpret_cast<const char*>(&six), sizeof(six));
        patch.close();
        CHECK(file.open(peaks, source) == TPeakFile::Status::Invalid, "TRAVPEAK version 6 opens");
        QFile legacy(peaks);
        CHECK(legacy.open(QIODevice::WriteOnly), "legacy");
        legacy.write(QByteArray("TRAVPF\0\0", 8) + QByteArray(200, '\0'));
        legacy.close();
        CHECK(file.open(peaks, source) == TPeakFile::Status::Invalid, "a legacy TRAVPF file opens");
    }

    // The source's time may differ by 2 seconds (shares and FAT round it),
    // not more
    {
        const QString peaks = TPeakCache::path_for(takeB);
        const auto source = TPeakFile::SourceInfo::from_file(takeB, 48000, 1, 5000);
        TPeakFile file;
        for (const qint64 shift : {qint64(1999), qint64(-1999), qint64(2000), qint64(-2000)}) {
            write_peaks(peaks, takeB, shift);
            CHECK(file.open(peaks, source) == TPeakFile::Status::Ok, "a source time %lld ms off is refused", (long long)shift);
            file.close();
        }
        for (const qint64 shift : {qint64(2001), qint64(-2001), qint64(60000)}) {
            write_peaks(peaks, takeB, shift);
            CHECK(file.open(peaks, source) == TPeakFile::Status::Invalid, "a source time %lld ms off is taken", (long long)shift);
        }
    }

    // A recording writes its peak file where the waveform of the recorded
    // file looks, and that opens without a build
    {
        const QString audio = dir.filePath("a/recorded.wav");
        const QString peaks = TPeakCache::path_for(audio);   // before the file exists, as a recording asks
        TPeakStream recording(48000, 1, peaks, 2);
        CHECK(recording.open() == TPeakFile::Status::Ok, "recording: open");
        CHECK(QFile::exists(recording.part_path()) && recording.part_path().startsWith(
                  peaks + QStringLiteral(".%1-").arg(QCoreApplication::applicationPid())), "recording: the part file is not per process");
        std::vector<float> samples(20000, 0.25f);
        const float* channels[1] = {samples.data()};
        recording.append(channels, qint64(samples.size()));
        write_file(audio, 1234);   // the recorded audio file, complete
        CHECK(recording.finish(audio) == TPeakFile::Status::Ok, "recording: finish");
        CHECK(recording.publish() == TPeakFile::Status::Ok, "recording: publish");
        TPeakFile file;
        CHECK(file.open(TPeakCache::path_for(audio), TPeakFile::SourceInfo::from_file(audio, 48000, 1, qint64(samples.size())))
                  == TPeakFile::Status::Ok, "the recorded file's waveform would build again");
    }

    if (s_failures) {
        std::printf("%d failures\n", s_failures);
        return 1;
    }
    std::printf("all peak cache tests passed\n");
    return 0;
}
