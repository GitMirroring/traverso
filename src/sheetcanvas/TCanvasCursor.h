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

#pragma once

#include "TViewItem.h"
#include "TContextPointer.h"
#include <QTimer>
#include <QVariant>
#include <QString>

class TSheetView;
class TPositionIndicator;
class TCursorRenderer;

/**
 * @class TCanvasCursor
 * @brief Coordinates the visual composition, bounding geometry changes, and active strategy routing of the canvas editing cursor.
 */
class TCanvasCursor : public TViewItem
{
    Q_OBJECT
    Q_PROPERTY(QPointF position READ get_pos WRITE set_pos)

public:
    explicit TCanvasCursor(TSheetView* sheetView);
    virtual ~TCanvasCursor() override;

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    void set_text(const QString& text, int mseconds = -1);
    void set_canvas_cursor_type(TContextPointer::CursorType cursorType);
    void set_cursor_data(const QVariant& data);

public slots:
    QPointF get_pos() const { return pos(); }
    void set_pos(const QPointF& position) {
        setPos(position);
        update_textitem_pos();
    }

private slots:
    void timer_timeout();

private:
    void update_textitem_pos();
    void calculate_bounding_rect() override;

    TContextPointer::CursorType      m_cursorType{TContextPointer::CursorType::Default};
    QString                          m_primaryText;
    QTimer                           m_timer;
    QVariant                         m_liveData;

    TPositionIndicator*              m_positionIndicator;
    std::unique_ptr<TCursorRenderer> m_renderer;
};
