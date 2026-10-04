
#include "TChannelStripCursorRenderer.h"

#include "TThemer.h"

TChannelStripCursorRenderer::TChannelStripCursorRenderer()
    : m_faderTrack(std::make_unique<TFaderTrack>()) {}

void TChannelStripCursorRenderer::draw(QPainter *painter, const QVariant &liveData)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    const QRectF bounds = get_bounding_rect();
    const QColor themeColor = themer()->get_color("TrackPanel:text");
    const qreal margin = 6.0;

    // 1. Render Background
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(0, 0, 0, 145));
    painter->drawRoundedRect(bounds, 6, 6);

    // 2. Layout LCD Display Area (Fixed 18px height)
    const QRectF lcdRect(
        bounds.left() + margin,
        bounds.top() + margin,
        bounds.width() - (2.0 * margin),
        18.0
        );

    // 3. Dynamic LCD Value Routing and Position Calculations
    QString displayValue;
    qreal normalizedPos = 0.0;

    if (liveData.userType() == QMetaType::QString) {
        // User is actively typing numbers: Display the buffered keystroke string directly in the LCD
        displayValue = liveData.toString();

        // Extract the underlying numerical value from the string to let the knob preview the target position.
        // If the string is not yet a fully valid number (e.g. just a "-" sign), it defaults to 0.0f travel position safely.
        QString cleanNumber = displayValue;
        cleanNumber.remove(QStringLiteral(" dB"));
        bool ok;
        float typedDb = cleanNumber.toFloat(&ok);

        if (ok) {
            normalizedPos = get_normalized_knob_position(QVariant::fromValue(typedDb));
        } else {
            // If text cannot be evaluated yet (e.g., typing a solitary minus sign), fall back to the bottom track origin
            normalizedPos = 0.0;
        }
    } else {
        // Standard tracking mode: Route live floats to subclass formatting blocks
        displayValue = get_value_text(liveData);
        normalizedPos = get_normalized_knob_position(liveData);
    }

    // Draw LCD readout frame
    painter->setBrush(QColor(15, 15, 15, 200));
    painter->drawRoundedRect(lcdRect, 3, 3);

    QColor displayColor = themer()->get_color("Toolbar:icon-primary");
    if (!displayColor.isValid()) displayColor = QColor(255, 165, 0);
    painter->setPen(displayColor);
    painter->setFont(QFont(QStringLiteral("Segoe UI"), 8, QFont::Bold));
    painter->drawText(lcdRect, Qt::AlignCenter, displayValue);

    // 4. Fetch specific channel strip configuration data
    const Qt::Orientation orientation = get_hardware_orientation();
    const QVector<TFaderTrack::TickInfo> ticks = get_tick_positions();

    // 5. Layout Track Area (The remaining canvas area underneath the LCD readout)
    const qreal trackAreaTop = lcdRect.bottom() + margin;
    const QRectF trackArea(
        bounds.left() + margin,
        trackAreaTop,
        bounds.width() - (2.0 * margin),
        bounds.bottom() - trackAreaTop - margin
        );

    // Render the track natively inside the track area bounding rect passed directly as an object
    m_faderTrack->draw(painter, trackArea, normalizedPos, ticks, themeColor, orientation);

    painter->restore();
}
