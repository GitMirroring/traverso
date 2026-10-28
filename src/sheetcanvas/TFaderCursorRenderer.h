// TFaderCursorRenderer.h
#pragma once
#include "TChannelStripCursorRenderer.h"
#include "TFaderLaw.h"

class TFaderCursorRenderer : public TChannelStripCursorRenderer
{
public:
    TFaderCursorRenderer() = default;
    virtual ~TFaderCursorRenderer() override = default;

    QRectF get_bounding_rect() const override {
        return QRectF(-40.0, -10.0, 80.0, 350.0);
    }

protected:
    QString get_value_text(const QVariant& liveData) const override {
        float db = liveData.toFloat();

        if (db <= TFaderLaw::MinDB) {
            return QStringLiteral("-inf dB");
        }

        return QString("%1 dB").arg(db, 0, 'f', 1);
    }

    qreal get_normalized_knob_position(const QVariant& liveData) const override {
        return static_cast<qreal>(TFaderLaw::dB_to_position(liveData.toFloat()));
    }

    Qt::Orientation get_hardware_orientation() const override {
        return Qt::Vertical;
    }

    QVector<TFaderTrack::TickInfo> get_tick_positions() const override {
        QVector<TFaderTrack::TickInfo> ticks;

        ticks.append({TFaderLaw::dB_to_position(12.0),   QStringLiteral("+12")});
        ticks.append({TFaderLaw::dB_to_position(6.0),    QStringLiteral("+6")});

        ticks.append({TFaderLaw::dB_to_position(0.0),    QStringLiteral("0")});
        ticks.append({TFaderLaw::dB_to_position(-3.0),   QStringLiteral("-3")});  // Power halving
        ticks.append({TFaderLaw::dB_to_position(-6.0),   QStringLiteral("-6")});  // Amplitude halving
        ticks.append({TFaderLaw::dB_to_position(-10.0),  QStringLiteral("-10")}); // Perceived loudness halving
        ticks.append({TFaderLaw::dB_to_position(-15.0),  QStringLiteral("-15")});
        ticks.append({TFaderLaw::dB_to_position(-20.0),  QStringLiteral("-20")});
        ticks.append({TFaderLaw::dB_to_position(-30.0),  QStringLiteral("-30")});

        ticks.append({TFaderLaw::dB_to_position(-40.0),  QStringLiteral("-40")});
        ticks.append({TFaderLaw::dB_to_position(-60.0),  QStringLiteral("-60")});
        ticks.append({TFaderLaw::dB_to_position(TFaderLaw::MinDB), QStringLiteral("-inf")});

        return ticks;
    }



};
