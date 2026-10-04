// TPanningCursorRenderer.h
#pragma once
#include "TChannelStripCursorRenderer.h"
#include <algorithm>
#include <cmath>

/**
 * @class TPanningCursorRenderer
 * @brief Horizontal channel strip renderer that visualizes stereo panning balances, proportioned identically to the fader's length.
 */
class TPanningCursorRenderer : public TChannelStripCursorRenderer
{
public:
    TPanningCursorRenderer() = default;
    virtual ~TPanningCursorRenderer() override = default;

    /**
     * @brief Defines the bounding canvas area scaled symmetrically to match the fader's total length (180w x 80h).
     */
    QRectF get_bounding_rect() const override {
        // Perfectly centered horizontally and vertically, matching the 180px length baseline of the fader
        return QRectF(-90.0, -40.0, 180.0, 80.0);
    }

protected:
    /**
     * @brief Converts raw stereo pan balance values (-1.0 to 1.0) into clear Center/Left/Right percentage strings.
     */
    QString get_value_text(const QVariant& liveData) const override {
        qreal pan = liveData.toFloat();
        if (qFuzzyIsNull(pan)) {
            return QStringLiteral("Center");
        }

        int percentage = std::round(std::abs(pan) * 100.0);
        return pan < 0 ? QString("L %1").arg(std::clamp(percentage, 0, 100))
                       : QString("R %1").arg(std::clamp(percentage, 0, 100));
    }

    /**
     * @brief Maps standard panning coefficients (-1.0 to 1.0) into a linear normalized fraction (0.0 to 1.0).
     */
    qreal get_normalized_knob_position(const QVariant& liveData) const override {
        qreal pan = liveData.toFloat();
        return qreal(std::clamp((pan + 1.0) / 2.0, 0.0, 1.0));
    }

    /**
     * @brief Configures the component to use horizontal coordinate layout rules.
     */
    Qt::Orientation get_hardware_orientation() const override {
        return Qt::Horizontal;
    }

    /**
     * @brief Positions spatial boundary anchors (Left, Center, Right) across the horizontal track plane.
     */
    QVector<TFaderTrack::TickInfo> get_tick_positions() const override {
        return {
            {0.0, QStringLiteral("L")},
            {0.5, QStringLiteral("C")},
            {1.0, QStringLiteral("R")}
        };
    }
};
