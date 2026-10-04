// TAutomationCursorRenderer.cpp
#include "TAutomationCursorRenderer.h"
#include "TThemer.h"
#include <QPointF>

QRectF TAutomationCursorRenderer::get_bounding_rect() const
{
    return QRectF(-55.0, -35.0, 110.0, 80.0);
}

void TAutomationCursorRenderer::draw(QPainter* painter, const QVariant& liveData)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    const QRectF bounds = get_bounding_rect();
    QColor themeColor = themer()->get_color("TrackPanel:text");
    if (!themeColor.isValid()) themeColor = QColor(225, 225, 225);
    QColor accentColor = themer()->get_color("Toolbar:icon-primary");
    if (!accentColor.isValid()) accentColor = QColor(255, 165, 0); // Traverso Oranje

    // Unpack data and check if we are actively dragging (passed down as a specific string token prefix)
    QString rawString = liveData.isValid() ? liveData.toString() : QStringLiteral("0.0 dB");
    bool isDragging = rawString.startsWith(QLatin1String("[DRAG]"));
    QString displayValue = isDragging ? rawString.mid(6) : rawString;

    // 1. Render uniform LCD Display Area
    const QRectF lcdRect(bounds.left() + 8.0, bounds.bottom() - 24.0, bounds.width() - 16.0, 20.0);
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(12, 12, 12, 210));
    painter->drawRoundedRect(lcdRect, 3.5, 3.5);

    painter->setPen(accentColor);
    painter->setFont(QFont(QStringLiteral("Segoe UI"), 9, QFont::Bold));
    painter->drawText(lcdRect, Qt::AlignCenter, displayValue);

    // 2. Render High-Precision Target Ring (Centered at Y: -10.0)
    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(accentColor, 1.5, Qt::SolidLine, Qt::RoundCap));

    const qreal centerY = -10.0;
    const qreal radius = 5.0;
    painter->drawEllipse(QPointF(0.0, centerY), radius, radius);

    // 3. Contextual Whiskers: Draw as sharp lines during hover, or arrows during drag!
    painter->setPen(QPen(themeColor, 1.5, Qt::SolidLine, Qt::RoundCap));

    // Left & Right lines
    painter->drawLine(QPointF(-11.0, centerY), QPointF(-5.0, centerY));
    painter->drawLine(QPointF(5.0, centerY), QPointF(11.0, centerY));
    // Top & Bottom lines
    painter->drawLine(QPointF(0.0, centerY - 11.0), QPointF(0.0, centerY - 5.0));
    painter->drawLine(QPointF(0.0, centerY + 5.0), QPointF(0.0, centerY + 10.0));

    if (isDragging) {
        // DYNAMIC SHIFT: Draw tiny arrow chevrons on the outer tips to signal translation movement
        painter->setPen(QPen(accentColor, 1.5, Qt::SolidLine, Qt::RoundCap));
        // Left arrow tip
        painter->drawLine(QPointF(-9.0, centerY - 2.5), QPointF(-11.0, centerY));
        painter->drawLine(QPointF(-9.0, centerY + 2.5), QPointF(-11.0, centerY));
        // Right arrow tip
        painter->drawLine(QPointF(9.0, centerY - 2.5), QPointF(11.0, centerY));
        painter->drawLine(QPointF(9.0, centerY + 2.5), QPointF(11.0, centerY));
        // Top arrow tip
        painter->drawLine(QPointF(-2.5, -19.0), QPointF(0.0, -21.0));
        painter->drawLine(QPointF(2.5, -19.0), QPointF(0.0, -21.0));
        // Bottom arrow tip
        painter->drawLine(QPointF(-2.5, -1.0), QPointF(0.0, 1.0));
        painter->drawLine(QPointF(2.5, -1.0), QPointF(0.0, 1.0));
    }

    painter->restore();
}
