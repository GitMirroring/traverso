#include "TFaderTrack.h"

#include <QPainterPath>

void TFaderTrack::draw(QPainter *painter, const QRectF &trackArea, qreal normalizedPosition, const QVector<TickInfo> &ticks, const QColor &themeColor, Qt::Orientation orientation)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    const qreal margin = 12.0; // Line padding constraint from edges

    if (orientation == Qt::Vertical) {
        // --- VERTICAL HARDWARE PROFILE (Fader Look) ---
        // Shift the center axis to 65% of the track area width to make room for text on the left
        const qreal centerX = trackArea.left() + (trackArea.width() * 0.65);
        const qreal usableHeight = trackArea.height() - (margin * 2.0);

        // 1. Draw physical vertical slot line
        painter->setPen(QPen(themeColor.darker(180), 1.5, Qt::SolidLine, Qt::RoundCap));
        painter->drawLine(centerX, trackArea.top() + margin, centerX, trackArea.bottom() - margin);

        // 2. Compute classic rectangular fader knob bounding box
        const qreal knobY = trackArea.bottom() - margin - (normalizedPosition * usableHeight);
        const qreal knobWidth = 24.0;
        const qreal knobHeight = 12.0;
        const QRectF knobRect(centerX - (knobWidth / 2.0), knobY - (knobHeight / 2.0), knobWidth, knobHeight);

        // 3. Render tickmarks and labels strictly on the LEFT side of the fader
        painter->setPen(QPen(themeColor.darker(140), 1));
        // painter->setFont(QFont(QStringLiteral("Segoe UI"), 7));

        // Pushed further left: 6px spacing from knob edge instead of 2px
        const qreal tickRight = centerX - (knobWidth / 2.0) - 6.0;
        const qreal tickLeft = tickRight - 4.0;                    // 4px tick mark width
        const qreal labelRight = tickLeft - 3.0;                   // 3px spacing to text labels

        for (const TickInfo& tick : ticks) {
            const qreal tickY = trackArea.bottom() - margin - (tick.position * usableHeight);

            // Draw horizontal line on the left side
            painter->drawLine(tickLeft, tickY, tickRight, tickY);

            if (!tick.label.isEmpty()) {
                // Comfortably fit "-inf" without clipping
                const QRectF textRect(labelRight - 35.0, tickY - 5.0, 35.0, 10.0);
                painter->drawText(textRect, Qt::AlignRight | Qt::AlignVCenter, tick.label);
            }
        }

        // 4. Draw the fader knob asset
        draw_fader_knob(painter, knobRect, themeColor, orientation);

    } else {
        // --- HORIZONTAL HARDWARE PROFILE (Panner Look) ---
        // REFINED LAYOUT FIX: Adjusted centerY to 55% to bring the line slightly higher up.
        const qreal parentHeight = trackArea.bottom() + 12.0;
        const qreal centerY = parentHeight * 0.55;

        const qreal usableWidth = trackArea.width() - (margin * 2.0);

        // 1. Draw physical horizontal slot line
        painter->setPen(QPen(themeColor.darker(180), 1.5, Qt::SolidLine, Qt::RoundCap));
        painter->drawLine(trackArea.left() + margin, centerY, trackArea.right() - margin, centerY);

        // 2. Compute classic rectangular panning knob bounding box
        const qreal knobX = trackArea.left() + margin + (normalizedPosition * usableWidth);
        const qreal knobWidth = 12.0;
        const qreal knobHeight = 24.0;
        const QRectF knobRect(knobX - (knobWidth / 2.0), centerY - (knobHeight / 2.0), knobWidth, knobHeight);

        // 3. Render tickmarks and labels on the TOP side of the track
        painter->setPen(QPen(themeColor.darker(140), 1));
        painter->setFont(QFont(QStringLiteral("Segoe UI"), 7));

        // EXPANDED CLEARANCE: Pushed tickmarks further up from the fader line and knob edge
        const qreal tickBottom = centerY - (knobHeight / 2.0) - 6.0; // Increased clearance from 2.0 to 6.0px
        const qreal tickTop = tickBottom - 4.0;
        const qreal labelBottom = tickTop - 2.0;

        for (const TickInfo& tick : ticks) {
            const qreal tickX = trackArea.left() + margin + (tick.position * usableWidth);

            // Draw vertical line on the top side
            painter->drawLine(tickX, tickTop, tickX, tickBottom);

            if (!tick.label.isEmpty()) {
                const QRectF textRect(tickX - 15.0, labelBottom - 10.0, 30.0, 10.0);
                painter->drawText(textRect, Qt::AlignCenter, tick.label);
            }
        }

        // 4. Draw the horizontal panning knob asset
        draw_fader_knob(painter, knobRect, themeColor, orientation);
    }

    painter->restore();
}

void TFaderTrack::draw_fader_knob(QPainter *painter, const QRectF &knobRect, const QColor &themeColor, Qt::Orientation orientation)
{
    painter->save();
    painter->setBrush(themeColor);
    painter->setPen(QPen(themeColor, 1));

    QPainterPath knobPath;
    const qreal cornerCurve = (orientation == Qt::Vertical) ? knobRect.width() * 0.15 : knobRect.height() * 0.15;
    const qreal indent = (orientation == Qt::Vertical) ? knobRect.height() * 0.25 : knobRect.width() * 0.25;

    if (orientation == Qt::Vertical) {
        knobPath.moveTo(knobRect.left(), knobRect.top());
        knobPath.lineTo(knobRect.left(), knobRect.bottom());
        knobPath.quadTo(knobRect.left() + cornerCurve, knobRect.bottom(), knobRect.left() + (knobRect.width() / 2.0), knobRect.bottom() - indent);
        knobPath.quadTo(knobRect.right() - cornerCurve, knobRect.bottom(), knobRect.right(), knobRect.bottom());
        knobPath.lineTo(knobRect.right(), knobRect.top());
        knobPath.quadTo(knobRect.right() - cornerCurve, knobRect.top(), knobRect.left() + (knobRect.width() / 2.0), knobRect.top() + indent);
        knobPath.quadTo(knobRect.left() + cornerCurve, knobRect.top(), knobRect.left(), knobRect.top());
    } else {
        knobPath.moveTo(knobRect.left(), knobRect.top());
        knobPath.lineTo(knobRect.right(), knobRect.top());
        knobPath.quadTo(knobRect.right(), knobRect.top() + cornerCurve, knobRect.right() - indent, knobRect.top() + (knobRect.height() / 2.0));
        knobPath.quadTo(knobRect.right(), knobRect.bottom() - cornerCurve, knobRect.right(), knobRect.bottom());
        knobPath.lineTo(knobRect.left(), knobRect.bottom());
        knobPath.quadTo(knobRect.left(), knobRect.bottom() - cornerCurve, knobRect.left() + indent, knobRect.top() + (knobRect.height() / 2.0));
        knobPath.quadTo(knobRect.left(), knobRect.top() + cornerCurve, knobRect.left(), knobRect.top());
    }
    painter->drawPath(knobPath);

    painter->setPen(QPen(QColor(15, 15, 15), 1.5));
    if (orientation == Qt::Vertical) {
        const qreal lineCenterY = knobRect.top() + (knobRect.height() / 2.0);
        painter->drawLine(knobRect.left() + 1, lineCenterY, knobRect.right() - 1, lineCenterY);
    } else {
        const qreal lineCenterX = knobRect.left() + (knobRect.width() / 2.0);
        painter->drawLine(lineCenterX, knobRect.top() + 1, lineCenterX, knobRect.bottom() - 1);
    }

    painter->restore();
}
