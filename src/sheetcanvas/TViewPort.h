/*
    Copyright (C) 2005-2006 Remon Sijrier

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

#ifndef TVIEWPORT_H
#define TVIEWPORT_H

#include <QGraphicsView>
#include <QGraphicsItem>
#include <QTimer>
#include "TViewPortInterface.h"

class TViewItem;
class TSheetView;
class TContextItem;
class TAudioFileImportCommand;
class TAudioTrack;
class HoldCursor;
class QGraphicsTextItem;

class TViewPort : public QGraphicsView, public TViewPortInterface
{
    Q_OBJECT

public :
    TViewPort(QGraphicsScene* scene, QWidget* parent);
    virtual ~TViewPort();

    // Set functions
    void set_canvas_cursor_text(const QString& text, int mseconds=-1);
    void set_canvas_cursor_pos(QPointF pos, CursorMoveReason reason);
    void set_canvas_cursor_shape(const QString& shape, int alignment=Qt::AlignCenter);
    virtual void set_sheetview(TSheetView* view) {m_sv = view;}

    inline QPointF map_to_scene(const QPoint& pos) const {
        return mapToScene(pos);
    }

    inline QPoint map_to_global(const QPoint& point) const {
        return mapToGlobal(point);
    }

    void grab_mouse();
    void release_mouse();

    void detect_items_below_cursor();



protected:
    virtual bool event(QEvent *event);
    virtual void enterEvent (QEnterEvent * );
    virtual void leaveEvent ( QEvent * );
    virtual void paintEvent( QPaintEvent* e);
    virtual void mouseMoveEvent(QMouseEvent* e);
    virtual void mousePressEvent ( QMouseEvent * e );
    virtual void mouseReleaseEvent ( QMouseEvent * e );
    virtual void mouseDoubleClickEvent ( QMouseEvent * e );
    virtual void wheelEvent ( QWheelEvent* e );
    virtual void keyPressEvent ( QKeyEvent* e);
    virtual void keyReleaseEvent ( QKeyEvent* e);
    virtual void dragEnterEvent(QDragEnterEvent *event);
    virtual void dragMoveEvent(QDragMoveEvent *event);


    void tabletEvent ( QTabletEvent * event );

    TSheetView* m_sv;

private:
    QPoint      m_previousMousePos;
    QTimer      m_grabMouseGuardTimer;
    int         m_mouseGrabCheckTime;
};

#endif

//eof
