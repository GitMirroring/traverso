/*
Copyright (C) 2010-2026 Remon Sijrier

This file is part of Traverso

Traverso is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA.

*/

#include "TCanvasCursor.h"
#include "TPositionIndicator.h"
#include "TCursorRenderer.h"
#include "TViewPort.h"

TCanvasCursor::TCanvasCursor(TSheetView* )
    : TViewItem(nullptr)
    , m_renderer(std::make_unique<TCursorRenderer>())
{
    m_positionIndicator = new TPositionIndicator(this);
    m_positionIndicator->hide();

    set_ignore_context(true);
    setZValue(20000);

    connect(&m_timer, &QTimer::timeout, this, &TCanvasCursor::timer_timeout);
}

TCanvasCursor::~TCanvasCursor() {}

void TCanvasCursor::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    m_renderer->render(painter, m_liveData);
}

void TCanvasCursor::calculate_bounding_rect()
{
    if (!m_renderer) return;

    QRectF targetBounds = m_renderer->get_active_bounding_rect();
    if (m_boundingRect == targetBounds) {
        return;
    }

    prepareGeometryChange();
    m_boundingRect = targetBounds;
}

void TCanvasCursor::set_canvas_cursor_type(TContextPointer::CursorType cursorType)
{
    if (m_cursorType == cursorType && m_cursorType != TContextPointer::CursorType::Default) {
        return;
    }

    m_cursorType = cursorType;

    if (m_cursorType == TContextPointer::CursorType::Default) {
        m_liveData = QVariant();
    }

    m_renderer->update_strategy(m_cursorType);

    prepareGeometryChange();
    calculate_bounding_rect();
    update();
    update_textitem_pos();
}

void TCanvasCursor::set_cursor_data(const QVariant& data)
{
    if (m_liveData == data) {
        return;
    }

    m_liveData = data;
    update();
}

void TCanvasCursor::set_text(const QString & text, int mseconds)
{
    m_primaryText = text;

    if (m_cursorType == TContextPointer::CursorType::GainFader ||
        m_cursorType == TContextPointer::CursorType::Panning) {
        m_positionIndicator->hide();
        return; // Fail early to suppress external text items from painting
    }

    if (m_timer.isActive()) {
        m_timer.stop();
    }

    if (m_primaryText.isEmpty()) {
        m_positionIndicator->hide();
        return;
    }

    m_positionIndicator->set_text(m_primaryText);
    update_textitem_pos();
    m_positionIndicator->show();

    if (mseconds > 0) {
        m_timer.start(mseconds);
    }
}

void TCanvasCursor::update_textitem_pos()
{
    TViewPort* vp = static_cast<TViewPort*>(cpointer().get_viewport());
    if (!vp || !m_positionIndicator->isVisible()) {
        return;
    }

    // Default positioning rules for floating alphanumeric tool labels
    qreal textItemX = 25.0;
    int textItemY = 25;

    QPointF textPos(textItemX, textItemY);
    qreal xRightTextItem = vp->mapFromScene(scenePos()).x() + m_positionIndicator->boundingRect().width() + textItemX;
    qreal xLeftTextItem = vp->mapFromScene(scenePos()).x() + textItemX;
    int viewPortWidth = vp->width();

    // Bound checking constraints to prevent indicator clipping against view boundaries
    if (xLeftTextItem < 0) {
        textItemX = mapFromScene(vp->mapToScene(0, int(m_positionIndicator->scenePos().y()))).x();
    }

    if (xRightTextItem > viewPortWidth) {
        textItemX = mapFromScene(vp->mapToScene(viewPortWidth - int(m_positionIndicator->boundingRect().width()), int(m_positionIndicator->scenePos().y()))).x();
    }

    textPos.setX(textItemX);
    m_positionIndicator->setPos(textPos);
}

void TCanvasCursor::timer_timeout()
{
    set_text("");
}
