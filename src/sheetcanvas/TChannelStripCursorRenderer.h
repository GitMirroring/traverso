// TChannelStripCursorRenderer.h
#pragma once

#include "TBaseCursorRenderer.h"
#include "TFaderTrack.h"
#include <QVector>

/**
 * @class TChannelStripCursorRenderer
 * @brief Base renderer that handles common layout boundaries for the LCD display and the track canvas.
 */
class TChannelStripCursorRenderer : public TBaseCursorRenderer
{
public:
    TChannelStripCursorRenderer();

    virtual ~TChannelStripCursorRenderer() override = default;

    /**
     * @brief Renders the channel strip background, LCD readout, and dispatches the native track coordinates.
     */
    void draw(QPainter* painter, const QVariant& liveData) override;

protected:
    virtual QString get_value_text(const QVariant& liveData) const = 0;
    virtual qreal get_normalized_knob_position(const QVariant& liveData) const = 0;
    virtual Qt::Orientation get_hardware_orientation() const = 0;
    virtual QVector<TFaderTrack::TickInfo> get_tick_positions() const = 0;

private:
    std::unique_ptr<TFaderTrack> m_faderTrack;
};
