// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Benchmark: building the pyramid of a one hour 48 kHz stereo source, querying
// a 4K wide row of columns at several zooms, and painting the envelope with
// QPainter (raster, like TAudioClipView). Not part of ctest; build in Release.

#include "TPeakFile.h"
#include "TPeakPyramid.h"
#include "TPeakStream.h"
#include "TWaveformColumns.h"

#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QPolygonF>
#include <QTemporaryDir>

#include <cmath>
#include <cstdio>
#include <vector>

static constexpr uint Rate = 48000;
static constexpr int Channels = 2;
static constexpr qint64 Frames = qint64(Rate) * 3600;
static constexpr int Columns = 3840;
static constexpr int Height = 200;

static float signal_at(int channel, qint64 frame)
{
    const double t = double(frame) / Rate;
    const double envelope = 0.5 + 0.5 * std::sin(2.0 * M_PI * 0.05 * t + channel);
    return float(envelope * (0.7 * std::sin(2.0 * M_PI * 220.0 * t) + 0.2 * std::sin(2.0 * M_PI * 3520.0 * t + channel)));
}

// Computes the samples instead of decoding them: measures the query only
class GeneratedSource : public TWaveformSampleSource
{
public:
    qint64 read(qint64 firstFrame, qint64 count, float* const* channels) override
    {
        for (int c = 0; c < Channels; ++c) {
            for (qint64 i = 0; i < count; ++i) {
                channels[c][i] = signal_at(c, firstFrame + i);
            }
        }
        return count;
    }
};

template<typename Function>
static double best_ms(int repeats, Function function)
{
    double best = 1e30;
    for (int r = 0; r < repeats; ++r) {
        QElapsedTimer timer;
        timer.start();
        function();
        best = std::min(best, double(timer.nsecsElapsed()) / 1e6);
    }
    return best;
}

// The envelope of one channel: maxima left to right, minima back
static QPolygonF envelope(const TWaveformColumn* columns, int count, double center, double scale)
{
    QPolygonF polygon;
    polygon.reserve(count * 2);
    for (int i = 0; i < count; ++i) {
        polygon.append(QPointF(i + 0.5, center - scale * columns[i].max));
    }
    for (int i = count - 1; i >= 0; --i) {
        polygon.append(QPointF(i + 0.5, center - scale * columns[i].min));
    }
    return polygon;
}

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);

    // Build: level 0 appended, then post-processed
    QTemporaryDir dir;
    const QString path = dir.filePath("bench.peaks");
    const QString audio = dir.filePath("bench.wav");
    {
        QFile take(audio);
        if (!take.open(QIODevice::WriteOnly) || take.write("audio") != 5) {
            std::printf("cannot write %s\n", qPrintable(audio));
            return 1;
        }
    }
    TPeakStream stream(Rate, Channels, path);
    stream.open();
    std::vector<float> chunk(size_t(65536) * Channels);
    QElapsedTimer timer;
    double generateMs = 0.0;
    double appendMs = 0.0;
    for (qint64 position = 0; position < Frames; position += 65536) {
        const qint64 count = std::min<qint64>(65536, Frames - position);
        timer.start();
        for (int c = 0; c < Channels; ++c) {
            for (qint64 i = 0; i < count; ++i) {
                chunk[size_t(c) * 65536 + size_t(i)] = signal_at(c, position + i);
            }
        }
        generateMs += double(timer.nsecsElapsed()) / 1e6;
        const float* pointers[Channels] = {chunk.data(), chunk.data() + 65536};
        timer.start();
        stream.append(pointers, count);
        appendMs += double(timer.nsecsElapsed()) / 1e6;
    }
    timer.start();
    stream.finish(audio);
    appendMs += double(timer.nsecsElapsed()) / 1e6;
    std::printf("build: 1 h 48 kHz stereo, all levels %.0f ms (%.0fx real time), signal generation %.0f ms (not counted)\n",
                appendMs, 3600000.0 / appendMs, generateMs);

    // The waveform of a recording zoomed out over the whole hour: the
    // written file mapped for the read and its columns, on every full repaint
    std::vector<TWaveformColumn> liveColumns(size_t(Columns) * Channels);
    const TTimeRef wholeWidth(qint64(std::llround(double(Frames) / Columns * double(TTimeRef::UNIVERSAL_SAMPLE_RATE) / Rate)));
    const double liveMs = best_ms(5, [&] {
        stream.read([&](const TPeakLevels& live) {
            TWaveformColumns::query(live, nullptr, TTimeRef(), wholeWidth, Columns, liveColumns.data());
        });
    });
    // Ten minutes in view (minute 25 to 35), the widest view a fresh mapping
    // per repaint is meant for (doc/design/peak-records.md)
    const TTimeRef tenStart(qint64(std::llround(25.0 * 60.0 * double(TTimeRef::UNIVERSAL_SAMPLE_RATE))));
    const TTimeRef tenWidth(qint64(std::llround(600.0 / Columns * double(TTimeRef::UNIVERSAL_SAMPLE_RATE))));
    const double liveTenMs = best_ms(20, [&] {
        stream.read([&](const TPeakLevels& live) {
            TWaveformColumns::query(live, nullptr, tenStart, tenWidth, Columns, liveColumns.data());
        });
    });

    timer.start();
    while (stream.post_process()) {
    }
    const double postMs = double(timer.nsecsElapsed()) / 1e6;
    const TPeakFile::SourceInfo source = TPeakFile::SourceInfo::from_file(audio, Rate, Channels, Frames);
    TPeakFile file;
    const double openMs = best_ms(1, [&] { file.open(path, source); });
    const double fileTenMs = best_ms(20, [&] { TWaveformColumns::query(file.levels(), nullptr, tenStart, tenWidth, Columns, liveColumns.data()); });
    std::printf("file: header and name %.1f ms, %.1f MB, open + validate %.3f ms\n", postMs, double(QFileInfo(path).size()) / 1e6, openMs);
    std::printf("while written (mapped per read), %d columns: the whole hour %.2f ms, 10 minutes %.3f ms per repaint\n", Columns,
                liveMs, liveTenMs);
    std::printf("finished file (mapping kept), 10 minutes in %d columns: %.3f ms per repaint\n", Columns, fileTenMs);

    // Query and paint
    GeneratedSource generated;
    std::vector<TWaveformColumn> columns(size_t(Columns) * Channels);
    QImage image(Columns, Height * Channels, QImage::Format_ARGB32_Premultiplied);

    std::printf("\nms per 4K row of 2 channels, %d px high each\n", Height);
    std::printf("%12s %10s %9s %10s %9s %9s %9s %9s %9s\n", "frames/col", "source", "query", "polygon", "rects", "+outline",
                "lines", "opaque", "direct");
    for (double framesPerColumn : {0.25, 1.0, 8.0, 63.0, 64.0, 256.0, 4096.0, 65536.0, double(Frames) / Columns}) {
        const TTimeRef width(qint64(std::llround(framesPerColumn * double(TTimeRef::UNIVERSAL_SAMPLE_RATE) / Rate)));
        const TTimeRef start = framesPerColumn * Columns >= Frames
            ? TTimeRef() : TTimeRef(TTimeRef::ONE_MINUTE_UNIVERSAL_SAMPLE_RATE * 7 + 12345);
        TWaveformColumns::Source used = TWaveformColumns::Source::None;

        const double queryMs = best_ms(5, [&] {
            used = TWaveformColumns::query(file.levels(), &generated, start, width, Columns, columns.data());
        });

        auto paint = [&](bool pen) {
            image.fill(Qt::transparent);
            QPainter painter(&image);
            painter.setRenderHint(QPainter::Antialiasing, false);
            painter.setBrush(QColor(80, 140, 220, 150));
            painter.setPen(pen ? QPen(QColor(20, 40, 80), 0) : QPen(Qt::NoPen));
            for (int c = 0; c < Channels; ++c) {
                painter.drawPolygon(envelope(columns.data() + size_t(c) * Columns, Columns, Height * c + Height / 2.0, Height * 0.45));
            }
        };
        const double paintMs = framesPerColumn >= 32 && framesPerColumn < 128 ? -1.0 : best_ms(3, [&] { paint(false); });

        // One rect per column (the raster version of QSG column quads), at
        // least one pixel high, plus optional outlines through the maxima and
        // the minima with a cosmetic 1 px pen
        std::vector<QRectF> rects(static_cast<size_t>(Columns));
        QPolygonF top(Columns);
        QPolygonF bottom(Columns);
        auto paint_columns = [&](bool outline) {
            image.fill(Qt::transparent);
            QPainter painter(&image);
            painter.setRenderHint(QPainter::Antialiasing, false);
            for (int c = 0; c < Channels; ++c) {
                const TWaveformColumn* channel = columns.data() + size_t(c) * Columns;
                const double center = Height * c + Height / 2.0;
                const double scale = Height * 0.45;
                for (int i = 0; i < Columns; ++i) {
                    const double y0 = std::floor(center - scale * channel[i].max);
                    const double y1 = std::max(y0 + 1.0, std::ceil(center - scale * channel[i].min));
                    rects[size_t(i)] = QRectF(i, y0, 1.0, y1 - y0);
                    top[i] = QPointF(i + 0.5, center - scale * channel[i].max);
                    bottom[i] = QPointF(i + 0.5, center - scale * channel[i].min);
                }
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(80, 140, 220, 150));
                painter.drawRects(rects.data(), Columns);
                if (outline) {
                    painter.setPen(QPen(QColor(20, 40, 80), 0));
                    painter.drawPolyline(top);
                    painter.drawPolyline(bottom);
                }
            }
        };
        const double rectsMs = best_ms(5, [&] { paint_columns(false); });
        const double outlineMs = best_ms(5, [&] { paint_columns(true); });

        // The pixel span of every column
        auto span = [&](const TWaveformColumn& column, double center, int& y0, int& y1) {
            const double scale = Height * 0.45;
            y0 = int(std::floor(center - scale * column.max));
            y1 = std::max(y0 + 1, int(std::ceil(center - scale * column.min)));
        };

        // Variant: 1 px vertical lines, cosmetic pen
        std::vector<QLine> lines(static_cast<size_t>(Columns));
        const double linesMs = best_ms(5, [&] {
            image.fill(Qt::transparent);
            QPainter painter(&image);
            painter.setPen(QPen(QColor(80, 140, 220, 150), 0));
            for (int c = 0; c < Channels; ++c) {
                const TWaveformColumn* channel = columns.data() + size_t(c) * Columns;
                for (int i = 0; i < Columns; ++i) {
                    int y0, y1;
                    span(channel[i], Height * c + Height / 2.0, y0, y1);
                    lines[size_t(i)] = QLine(i, y0, i, y1 - 1);
                }
                painter.drawLines(lines.data(), Columns);
            }
        });

        // Variant: opaque integer rects
        std::vector<QRect> intRects(static_cast<size_t>(Columns));
        const double opaqueMs = best_ms(5, [&] {
            image.fill(Qt::transparent);
            QPainter painter(&image);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(80, 140, 220));
            for (int c = 0; c < Channels; ++c) {
                const TWaveformColumn* channel = columns.data() + size_t(c) * Columns;
                for (int i = 0; i < Columns; ++i) {
                    int y0, y1;
                    span(channel[i], Height * c + Height / 2.0, y0, y1);
                    intRects[size_t(i)] = QRect(i, y0, 1, y1 - y0);
                }
                painter.drawRects(intRects.data(), Columns);
            }
        });

        // Variant: spans written straight into the image, premultiplied
        // source-over blend, then the image drawn once (like a paint event
        // would do on the viewport)
        QImage target(Columns, Height * Channels, QImage::Format_ARGB32_Premultiplied);
        const double directMs = best_ms(5, [&] {
            image.fill(Qt::transparent);
            const quint32 alpha = 150;
            const quint32 color = (alpha << 24) | ((80 * alpha / 255) << 16) | ((140 * alpha / 255) << 8) | (220 * alpha / 255);
            const quint32 inverse = 255 - alpha;
            for (int c = 0; c < Channels; ++c) {
                const TWaveformColumn* channel = columns.data() + size_t(c) * Columns;
                for (int i = 0; i < Columns; ++i) {
                    int y0, y1;
                    span(channel[i], Height * c + Height / 2.0, y0, y1);
                    y0 = std::max(y0, 0);
                    y1 = std::min(y1, image.height());
                    for (int y = y0; y < y1; ++y) {
                        quint32* pixel = reinterpret_cast<quint32*>(image.scanLine(y)) + i;
                        const quint32 d = *pixel;
                        const quint32 rb = (((d & 0x00ff00ff) * inverse) >> 8) & 0x00ff00ff;
                        const quint32 ag = (((d >> 8) & 0x00ff00ff) * inverse) & 0xff00ff00;
                        *pixel = color + rb + ag;
                    }
                }
            }
            target.fill(Qt::white);
            QPainter painter(&target);
            painter.drawImage(0, 0, image);
        });

        std::printf("%12.2f %10s %9.3f %10.3f %9.3f %9.3f %9.3f %9.3f %9.3f\n", framesPerColumn,
                    used == TWaveformColumns::Source::Samples ? "samples" : used == TWaveformColumns::Source::Pyramid ? "pyramid" : "none",
                    queryMs, paintMs, rectsMs, outlineMs, linesMs, opaqueMs, directMs);
    }

    return 0;
}
