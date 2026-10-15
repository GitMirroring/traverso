// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Benchmark of drawing a curve the way the curve views do: building the
// polyline and painting it antialiased with a 1 px pen (TCurveView), or filling
// the area under it (TFadeCurveView).
//
// Compares the legacy way (one value per pixel, every third or every pixel,
// plus the node positions, sorted by x) with the pixel columns of
// TCurveSnapshot reduced to 0.5 px.
//
// Runs headless on an image as wide as a 4K screen, at device pixel ratio 1
// and 2, the latter also with cosmetic pens. Every case repeats whole repaints for at least the given number of
// seconds (default 1) and reports the process CPU time per repaint. Build it
// in Release: a Debug build mostly measures the missing optimisation.
//
// Usage: traverso_curve_paint_benchmark [seconds per case] [directory]
// With a directory it also writes a PNG per curve and method, 800 px of the
// curve at 2x device pixel ratio with the cosmetic pen, to compare the looks.

#include "TCurvePolyline.h"
#include "TCurveSnapshot.h"

#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <random>
#include <vector>

using Point = TCurveSnapshot::Point;

static const int    WIDTH = 3840;                           // pixels, a 4K screen
static const qreal  HEIGHT = 160.0;                         // pixels, a tall track
static const double ONE_MINUTE = 1693440000.0;              // universal frames
static const double SCALE = 10.0 * ONE_MINUTE / WIDTH;      // 10 minutes on screen
static const double START = 60.0 * ONE_MINUTE;              // one hour into the timeline

static double g_secondsPerCase = 1.0;
static QString g_imageDirectory;

struct Curve
{
    const char*        name;
    std::vector<Point> points;
};

static Curve automation_curve(std::mt19937& rng)
{
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    Curve curve{"automation, 60 nodes", {}};
    for (int i = 0; i < 60; ++i) {
        curve.points.push_back({START + (i + unit(rng) * 0.8) * WIDTH * SCALE / 60.0, unit(rng)});
    }
    return curve;
}

static Curve micro_edit_curve(std::mt19937& rng)
{
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    Curve curve{"micro edits, 60 groups x 40 nodes", {}};
    for (int group = 0; group < 60; ++group) {
        const double groupStart = START + (group + 0.2) * WIDTH * SCALE / 60.0;
        for (int i = 0; i < 40; ++i) {
            // 40 nodes within 2 pixels, alternating high and low
            const double value = (i % 2) ? 0.6 + 0.4 * unit(rng) : 0.4 * unit(rng);
            curve.points.push_back({groupStart + i * 2.0 * SCALE / 40.0, value});
        }
    }
    return curve;
}

static Curve dense_curve(std::mt19937& rng)
{
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    Curve curve{"dense, 20000 nodes", {}};
    for (int i = 0; i < 20000; ++i) {
        curve.points.push_back({START + (i + 0.5) * WIDTH * SCALE / 20000.0, 0.5 + 0.5 * std::sin(i * 0.01) * unit(rng)});
    }
    return curve;
}

static Curve fade_curve()
{
    Curve curve{"fade, 12 nodes", {}};
    for (int i = 0; i <= 11; ++i) {
        const double f = i / 11.0;
        curve.points.push_back({START + f * WIDTH * SCALE, f * f * (3.0 - 2.0 * f)});
    }
    return curve;
}

static qreal y_of(double value)
{
    return HEIGHT - value * HEIGHT;
}

// TCurveView before: one value per pixel, every step-th of them plus the
// position of every node on screen, sorted by x
static QPolygonF legacy_polyline(const TCurveSnapshot& snapshot, const std::vector<Point>& points, int step,
                                 std::vector<float>& buffer, TCurveLookupCache& cache)
{
    snapshot.get_vector(START, START + WIDTH * SCALE, buffer.data(), nframes_t(WIDTH), cache);

    QPolygonF polyline;
    for (int i = 0; i < WIDTH; i += step) {
        polyline << QPointF(i, y_of(buffer[size_t(i)]));
    }
    for (const Point& point : points) {
        const qreal x = (point.when - START) / SCALE;
        if (x > 0 && x < WIDTH) {
            polyline << QPointF(x, y_of(point.value));
        }
    }
    std::sort(polyline.begin(), polyline.end(), [](const QPointF& left, const QPointF& right) {
        return left.x() < right.x();
    });
    return polyline;
}

static QPolygonF column_polyline(const TCurveSnapshot& snapshot, qreal tolerance,
                                 std::vector<TCurveColumn>& columns, TCurveLookupCache& cache)
{
    snapshot.get_columns(START, SCALE, WIDTH, columns.data(), cache);
    return curve_polyline(columns.data(), WIDTH, 0.0, HEIGHT, tolerance);
}

// Process CPU time of repeating frame() for at least g_secondsPerCase, in
// microseconds per frame
static double measure(const std::function<void()>& frame)
{
    frame();   // warm up

    int frames = 0;
    const std::clock_t cpuStart = std::clock();
    const auto wallStart = std::chrono::steady_clock::now();

    do {
        frame();
        ++frames;
    } while (std::chrono::duration<double>(std::chrono::steady_clock::now() - wallStart).count() < g_secondsPerCase);

    const double cpuSeconds = double(std::clock() - cpuStart) / CLOCKS_PER_SEC;
    return cpuSeconds * 1.0e6 / frames;
}

struct Method
{
    const char*                     name;
    std::function<QPolygonF()>      build;
};

static void run_curve(const Curve& curve, bool fill)
{
    auto snapshot = TCurveSnapshot::create(curve.points, 1.0);
    std::vector<float> buffer(static_cast<size_t>(WIDTH));
    std::vector<TCurveColumn> columns(static_cast<size_t>(WIDTH));
    TCurveLookupCache cache;

    std::vector<Method> methods = {
        {"legacy, every 3rd pixel", [&]() { return legacy_polyline(*snapshot, curve.points, 3, buffer, cache); }},
        {"legacy, every pixel", [&]() { return legacy_polyline(*snapshot, curve.points, 1, buffer, cache); }},
        {"columns, not reduced", [&]() { return column_polyline(*snapshot, 0.0, columns, cache); }},
        {"columns, reduced 0.5 px", [&]() { return column_polyline(*snapshot, 0.5, columns, cache); }},
    };

    if (!g_imageDirectory.isEmpty()) {
        // The first 800 pixels, every method below the other
        const int dpr = 2;
        const int shown = 800;
        QImage image(shown * dpr, int(methods.size() * (HEIGHT + 20)) * dpr, QImage::Format_ARGB32_Premultiplied);
        image.fill(QColor(40, 40, 40));
        QPainter painter(&image);
        painter.scale(dpr, dpr);
        painter.setRenderHint(QPainter::Antialiasing);
        for (size_t m = 0; m < methods.size(); ++m) {
            painter.save();
            painter.translate(0, qreal(m) * (HEIGHT + 20) + 10);
            painter.setPen(QColor(90, 90, 90));
            painter.drawText(QPointF(4, -1), methods[m].name);
            QPen pen(QColor(255, 200, 0));
            pen.setWidth(1);
            pen.setCosmetic(true);
            painter.setPen(fill ? QPen(Qt::NoPen) : pen);
            painter.setBrush(QColor(255, 200, 0, 120));
            painter.setClipRect(QRectF(0, 0, shown, HEIGHT));
            QPolygonF polyline = methods[m].build();
            if (fill) {
                QPainterPath path;
                path.addPolygon(polyline);
                path.lineTo(WIDTH, 0);
                path.lineTo(0, 0);
                path.closeSubpath();
                painter.drawPath(path);
            } else {
                painter.drawPolyline(polyline);
            }
            painter.restore();
        }
        painter.end();
        QString name = QString::fromLatin1(curve.name).section(',', 0, 0).replace(' ', '-');
        image.save(g_imageDirectory + "/curve-" + name + ".png");
    }

    std::printf("\n%s%s\n", curve.name, fill ? ", filled like a fade" : "");
    std::printf("  %-26s %7s %9s %13s %13s %13s %13s\n", "method", "points", "build us", "paint 1x us", "2x us", "2x cosm. 1px", "2x cosm. 2px");

    for (const Method& method : methods) {
        const qsizetype points = method.build().size();
        const double buildTime = measure([&]() { volatile qsizetype n = method.build().size(); (void)n; });

        // At 1x and 2x with the pen TCurveView uses (1 px wide, scales with
        // the device pixel ratio), and at 2x with cosmetic pens of 1 and 2
        // device pixels (their width does not scale)
        struct Setup { int dpr; bool cosmetic; int width; };
        const Setup setups[] = {{1, false, 1}, {2, false, 1}, {2, true, 1}, {2, true, 2}};
        double paintTimes[4];
        for (int setup = 0; setup < 4; ++setup) {
            const int dpr = setups[setup].dpr;
            if (fill && setups[setup].cosmetic) {
                paintTimes[setup] = 0.0;   // a fill has no pen
                continue;
            }
            QImage image(int(WIDTH * dpr), int(HEIGHT * dpr) + dpr * 2, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            painter.scale(dpr, dpr);
            painter.setRenderHint(QPainter::Antialiasing);
            QPen pen(QColor(255, 200, 0));
            pen.setWidth(setups[setup].width);
            pen.setCosmetic(setups[setup].cosmetic);
            painter.setPen(fill ? QPen(Qt::NoPen) : pen);
            painter.setBrush(QColor(255, 200, 0, 120));

            paintTimes[setup] = measure([&]() {
                QPolygonF polyline = method.build();
                if (fill) {
                    QPainterPath path;
                    path.addPolygon(polyline);
                    path.lineTo(WIDTH, 0);
                    path.lineTo(0, 0);
                    path.closeSubpath();
                    painter.drawPath(path);
                } else {
                    painter.drawPolyline(polyline);
                }
            });
        }

        std::printf("  %-26s %7lld %9.1f %13.1f %13.1f %13.1f %13.1f\n", method.name, qlonglong(points), buildTime,
                    paintTimes[0], paintTimes[1], paintTimes[2], paintTimes[3]);
    }

    if (fill) {
        return;
    }

    // Painting only, from a polyline or a QPainterPath built once before:
    // shows whether QPainter reuses anything of a path it drew before
    QPolygonF polyline = column_polyline(*snapshot, 0.0, columns, cache);
    QPainterPath path;
    path.addPolygon(polyline);

    std::printf("  painting only, built once:  %13s %13s\n", "1x us", "2x cosm. 1px");
    for (int usePath = 0; usePath < 2; ++usePath) {
        double times[2];
        for (int dpr = 1; dpr <= 2; ++dpr) {
            QImage image(int(WIDTH * dpr), int(HEIGHT * dpr) + dpr * 2, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            painter.scale(dpr, dpr);
            painter.setRenderHint(QPainter::Antialiasing);
            QPen pen(QColor(255, 200, 0));
            pen.setWidth(1);
            pen.setCosmetic(dpr == 2);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            times[dpr - 1] = measure([&]() {
                if (usePath) {
                    painter.drawPath(path);
                } else {
                    painter.drawPolyline(polyline);
                }
            });
        }
        std::printf("  %-26s %13.1f %13.1f\n", usePath ? "drawPath(QPainterPath)" : "drawPolyline(QPolygonF)", times[0], times[1]);
    }
}

int main(int argc, char** argv)
{
    if (argc > 1) {
        g_secondsPerCase = std::max(0.1, std::atof(argv[1]));
    }
    if (argc > 2) {
        g_imageDirectory = QString::fromLocal8Bit(argv[2]);
    }

    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QGuiApplication app(argc, argv);

    std::printf("Curve painting benchmark: %d x %.0f px, 10 minutes on screen one hour into the timeline,\n"
                "process CPU time per repaint of the whole width, %.1f s per case\n", WIDTH, HEIGHT, g_secondsPerCase);

    std::mt19937 rng(2026);
    run_curve(automation_curve(rng), false);
    run_curve(micro_edit_curve(rng), false);
    run_curve(dense_curve(rng), false);
    run_curve(fade_curve(), true);

    return 0;
}
