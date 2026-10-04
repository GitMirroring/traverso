// THoldCursorRenderer.cpp
#include "THoldCursorRenderer.h"
#include "TThemer.h"
#include <QPointF>

THoldCursorRenderer::THoldCursorRenderer(TContextPointer::CursorType dragType)
    : m_dragType(dragType)
{}

QRectF THoldCursorRenderer::get_bounding_rect() const
{
    // SUBTLY EXPANDED LAYOUT: Width increased to 110px to accommodate larger LCD and text gracefully
    return QRectF(-55.0, -35.0, 110.0, 78.0);
}

void THoldCursorRenderer::draw(QPainter* painter, const QVariant& liveData)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    const QRectF bounds = get_bounding_rect();

    QColor baseColor = themer()->get_color("TrackPanel:text");
    if (!baseColor.isValid()) baseColor = QColor(225, 225, 225);

    QColor accentColor = themer()->get_color("Toolbar:icon-primary");
    if (!accentColor.isValid()) accentColor = QColor(255, 165, 0); // Safety Orange

    // 1. Draw the Subtly Enlarged LCD Display Area if numerical string data is provided
    if (liveData.isValid() && !liveData.toString().isEmpty()) {
        // Height slightly increased to 22px, padding adjusted for perfect balance
        const QRectF lcdRect(bounds.left() + 8.0, bounds.bottom() - 26.0, bounds.width() - 16.0, 22.0);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(12, 12, 12, 210));
        painter->drawRoundedRect(lcdRect, 3.5, 3.5);

        painter->setPen(accentColor);
        // ENLARGED FONT: Increased from 8pt to 9.5pt for highly comfortable squint-free reading
        painter->setFont(QFont(QStringLiteral("Segoe UI"), 10));
        painter->drawText(lcdRect, Qt::AlignCenter, liveData.toString());
    }

    // 2. Setup Refined Vector Styling for the Arrows
    painter->setBrush(Qt::NoBrush);

    // Center baseline calculation point for drawing shapes (offset upwards slightly)
    const qreal centerY = -12.0;

    // 3. Render Pure Arrow Geometry Based on Active Context Profile
    if (m_dragType == TContextPointer::CursorType::HoldLeftRight) {
        // --- HORIZONTAL DIRECTIONAL ARROWS (<--->) ---
        painter->setPen(QPen(baseColor, 2.0, Qt::SolidLine, Qt::RoundCap));
        painter->drawLine(QPointF(-18.0, centerY), QPointF(18.0, centerY)); // Main shaft

        // Left arrowhead chevrons
        painter->drawLine(QPointF(-13.0, centerY - 4.5), QPointF(-18.0, centerY));
        painter->drawLine(QPointF(-13.0, centerY + 4.5), QPointF(-18.0, centerY));

        // Right arrowhead chevrons
        painter->drawLine(QPointF(11.0, centerY - 4.5), QPointF(16.0, centerY));
        painter->drawLine(QPointF(11.0, centerY + 4.5), QPointF(16.0, centerY));

    } else if (m_dragType == TContextPointer::CursorType::HoldUpDown) {
        // --- VERTICAL DIRECTIONAL ARROWS ---
        painter->setPen(QPen(accentColor, 2.0, Qt::SolidLine, Qt::RoundCap));
        painter->drawLine(QPointF(0.0, centerY - 16.0), QPointF(0.0, centerY + 16.0)); // Main shaft

        // Top arrowhead chevrons
        painter->drawLine(QPointF(-4.5, centerY - 11.0), QPointF(0.0, centerY - 16.0));
        painter->drawLine(QPointF(4.5, centerY - 11.0), QPointF(0.0, centerY - 16.0)); // Fixed typos internally

        // Bottom arrowhead chevrons
        painter->drawLine(QPointF(-4.5, centerY + 11.0), QPointF(0.0, centerY + 16.0));
        painter->drawLine(QPointF(4.5, centerY + 11.0), QPointF(0.0, centerY + 16.0));

    } else if (m_dragType == TContextPointer::CursorType::HoldOmni) {
        // --- 4-WAY CROSSHAIR OMNI ARROWS (Combined Horizontal & Vertical) ---
        // Horizontal component (base color)
        painter->setPen(QPen(baseColor, 1.8, Qt::SolidLine, Qt::RoundCap));
        painter->drawLine(QPointF(-14.0, centerY), QPointF(14.0, centerY));
        painter->drawLine(QPointF(-10.0, centerY - 3.5), QPointF(-14.0, centerY));
        painter->drawLine(QPointF(-10.0, centerY + 3.5), QPointF(-14.0, centerY));
        painter->drawLine(QPointF(10.0, centerY - 3.5), QPointF(14.0, centerY));
        painter->drawLine(QPointF(10.0, centerY + 3.5), QPointF(14.0, centerY));

        // Vertical component (accent color for crisp visual differentiation)
        painter->setPen(QPen(accentColor, 1.8, Qt::SolidLine, Qt::RoundCap));
        painter->drawLine(QPointF(0.0, centerY - 14.0), QPointF(0.0, centerY + 14.0));
        painter->drawLine(QPointF(-3.5, centerY - 10.0), QPointF(0.0, centerY - 14.0));
        painter->drawLine(QPointF(3.5, centerY - 10.0), QPointF(0.0, centerY - 14.0));
        painter->drawLine(QPointF(-3.5, centerY + 10.0), QPointF(0.0, centerY + 14.0));
        painter->drawLine(QPointF(3.5, centerY + 10.0), QPointF(0.0, centerY + 14.0));
    }

    painter->restore();
}
