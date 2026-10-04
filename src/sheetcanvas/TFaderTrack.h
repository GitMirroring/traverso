// TFaderTrack.h

#pragma once

#include <QPainter>
#include <QRectF>
#include <QVector>
#include <QString>

/**
 * @class TFaderTrack
 * @brief Renders an orientation-aware track with layout positions mapped natively to 2D screen coordinates.
 */
class TFaderTrack
{
public:
    struct TickInfo {
        qreal position;   // Normalized position between 0.0 and 1.0
        QString label;    // Text label string (can be empty)
    };

    TFaderTrack() = default;

    /**
     * @brief Draws the slider slot, labels, and knob dynamically matching vertical or horizontal orientation profiles.
     * @param trackArea The concrete screen bounding container allocated for the track visuals.
     */
    void draw(QPainter* painter, const QRectF& trackArea, qreal normalizedPosition,
              const QVector<TickInfo>& ticks, const QColor& themeColor, Qt::Orientation orientation);

private:
    /**
     * @brief Renders the customized fader knob geometry matching the active component orientation.
     */
    void draw_fader_knob(QPainter* painter, const QRectF& knobRect, const QColor& themeColor, Qt::Orientation orientation);
};
