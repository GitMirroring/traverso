/*
Copyright (C) 2006-2007 Remon Sijrier

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

#include "TCurveView.h"
#include "Debugger.h"
#include "TSheetView.h"
#include "TCurveNodeView.h"
#include <TThemer.h>

#include <TCurve.h>
#include <TCurveNode.h>
#include <TContextPointer.h>
#include <TSheet.h>
#include "TInputEventDispatcher.h"

#include "MoveCurveNode.h"
#include "TCurvePolyline.h"

#include <Debugger.h>


#define NODE_SOFT_SELECTION_DISTANCE 40


#include <cfloat>

TCurveView::TCurveView(TSheetView* sv, TViewItem *parentViewItem, TCurve* curve)
    : TViewItem(parentViewItem, curve)
    , m_curve(curve)
{
    setZValue(parentViewItem->zValue() + 1);
    setFlags(QGraphicsItem::ItemUsesExtendedStyleOption);

    m_sv = sv;
    TCurveView::load_theme_data();

    m_blinkColorDirection = 1;
    m_blinkingNode = nullptr;
    m_startoffset = TTimeRef();

    for (TCurveNode* node : m_curve->get_nodes()) {
        add_curvenode_view(node);
    }

    connect(&m_blinkTimer, &QTimer::timeout, this, &TCurveView::update_blink_color);
    connect(m_curve, &TCurve::nodeAdded, this, &TCurveView::add_curvenode_view);
    connect(m_curve, &TCurve::nodeRemoved, this, &TCurveView::remove_curvenode_view);
    connect(m_curve, &TCurve::nodePositionChanged, this, &TCurveView::node_moved);
    connect(m_curve, &TCurve::activeContextChanged, this, &TCurveView::active_context_changed);

    m_hasMouseTracking = true;
}

TCurveView::~ TCurveView( )
{
}

void TCurveView::updateNodeVisibility(int startx, int endx)
{
    // We should only show the nodes that are visible within the bounds of the current clip
    for (TCurveNodeView* nodeView : m_nodeViews) {
        int pos = nodeView->pos().x()+4 ; // +4 for half width of the node view
        nodeView->setVisible(pos >= startx && pos <= endx);    }
}

void TCurveView::paint( QPainter * painter, const QStyleOptionGraphicsItem * option, QWidget * widget )
{
    Q_UNUSED(widget);
    PENTER2;
    if (m_nodeViews.isEmpty()) {
        return;
    }

    if (item_ignores_context() && m_nodeViews.size() == 1) {
        return;
    }

    painter->save();

    // Path's need an additional pixel right/left to be painted correctly
    int xstart = int(option->exposedRect.x()) - 1;
    int pixelcount = int(option->exposedRect.width()) + 3;
    if (xstart < 0) {
        pixelcount += xstart;
        xstart = 0;
    }
    const qreal height = m_boundingRect.height();

    // A cosmetic pen of one device pixel: Qt draws wider pens (or a 1 pixel
    // pen scaled for a high resolution screen) with its general stroker,
    // which is 30 to 600 times slower (measured 2026-10-15 with the curve
    // paint benchmark of c3c1861a, since removed)
    QPen pen;
    pen.setWidth(1);
    pen.setCosmetic(true);

    QColor penColor;
    if (item_ignores_context()) {
        penColor = themer()->get_color("Curve:inactive");
    } else {
         penColor = themer()->get_color("Curve:active");
         QColor backgroundColor(0, 0, 0, 30);
         painter->fillRect(option->exposedRect, QBrush(backgroundColor));
    }
    pen.setColor(penColor);
    painter->setPen(pen);

    if (pixelcount <= 0) {
        painter->restore();
        return;
    }

    painter->setRenderHint(QPainter::Antialiasing);

    // Through the middle of the device pixels, so flat stretches are one
    // pixel row as before
    painter->translate(0.0, 0.5 / painter->device()->devicePixelRatioF());

    // One column per pixel, in time units: one pixel is timeref_scalefactor
    // universal frames. A column shows the lowest and highest value inside
    // it, so nodes closer together than a pixel stay visible.
    const double scale = double(m_sv->timeref_scalefactor);
    std::vector<TCurveColumn> columns(static_cast<size_t>(pixelcount));
    m_curve->get_columns(xstart * scale + double(m_startoffset.universal_frame()), scale, pixelcount, columns.data());

    // Not reduced: the time to paint a line depends on the pixels it covers,
    // not on its number of points
    painter->drawPolyline(curve_polyline(columns.data(), pixelcount, xstart, height, 0.0));

    painter->restore();
}

/**
 * @brief GUI slot triggered when a new core curve node keyframe has been added down the pipeline.
 *        Instantiates the matching graphical presentation view layer dynamically.
 */
void TCurveView::add_curvenode_view(TCurveNode* node)
{
    // Create the lightweight visual node element within the canvas scene context
    TCurveNodeView* nodeview = new TCurveNodeView(m_sv, this, node);
    m_nodeViews.append(nodeview);

    // Maintain strict chronological sorting across our visual presentation components
    std::sort(m_nodeViews.begin(), m_nodeViews.end(), [](TCurveNodeView* left, TCurveNodeView* right){
        return left->get_when() < right->get_when();
    });

    update();
    emit curveUpdated(0, int(m_boundingRect.width()));
}

/**
 * @brief GUI slot triggered when an existing core curve node keyframe is removed from the timeline data.
 *        Safely extracts and destroys the corresponding graphical element from the active canvas view.
 */
void TCurveView::remove_curvenode_view(TCurveNode* node)
{
    for (TCurveNodeView* nodeview : m_nodeViews) {
        if (nodeview->get_curve_node() == node) {
            m_nodeViews.removeAll(nodeview);

            // Sync soft-selection cursor properties if the discarded item was currently highlighted
            if (nodeview == m_blinkingNode) {
                m_blinkingNode = nullptr;
                update_softselected_node(cpointer().scene_pos());
            }

            // Cleanly purge the visual representation layout straight out of the graphics viewport
            scene()->removeItem(nodeview);
            delete nodeview;

            update();
            emit curveUpdated(0, int(m_boundingRect.width()));
            return;
        }
    }
}

void TCurveView::calculate_bounding_rect()
{
    // Add a bit of top/bottom margin so the curve line doesn't go all the
    // way to the top/bottom of the view, and as a side effect, the nodes
    // can be painted in the margin area, so they won't chop off.
    m_boundingRect = QRectF(0, 0, m_parentViewItem->boundingRect().width(), m_parentViewItem->get_height() - BORDER_MARGIN);
    setPos(0, BORDER_MARGIN / 2);
    TViewItem::calculate_bounding_rect();
}

void TCurveView::active_context_changed()
{
    if (has_active_context()) {
        m_blinkTimer.start(40);
    } else {
        if (ied().is_holding()) {
            TLog::write_printf(TLogLevel::Debug, TLOG_FILE_CATEGORY, "returning in active context changed");
            return;
        }

        m_blinkTimer.stop();
        if (m_blinkingNode) {
            m_blinkingNode->set_color(themer()->get_color("CurveNode:default"));
            m_blinkingNode->set_soft_selected(false);
            m_blinkingNode = nullptr;
        }

    }

    update(m_boundingRect);
}


void TCurveView::mouse_hover_move_event()
{
    update_softselected_node(cpointer().scene_pos());

    if (m_blinkingNode) {
        // 1. Instantly display our modern vectorized smart cursor during soft-selection hover
        cpointer().set_canvas_cursor_type(TContextPointer::CursorType::AutomationNode);

        // 2. Fetch the target node's actual current layout coefficient factor
        float currentVal = m_blinkingNode->get_curve_node()->get_value();
        float dbFactor = Mixer::coefficient_to_dB(currentVal);

        // 3. Render real-time feedback inside the LCD box during hover state
        if (dbFactor <= Mixer::min_fader_dB()) {
            cpointer().set_canvas_cursor_data(QVariant::fromValue(QStringLiteral("-inf dB")));
        } else {
            cpointer().set_canvas_cursor_data(QVariant::fromValue(QString("%1 dB").arg(dbFactor, 0, 'f', 1)));
        }
    } else {
        // Fall back cleanly to the OS default arrow cursor once the mouse exits the node's magnetic zone
        cpointer().set_canvas_cursor_type(TContextPointer::CursorType::Default);
    }
}


void TCurveView::update_softselected_node(QPointF point)
{
    if (m_nodeViews.isEmpty()) {
        return;
    }

    QPointF pos = mapToItem(this, point);

    TCurveNodeView* prevNode = m_blinkingNode;
    m_blinkingNode = m_nodeViews.first();

    if (! m_blinkingNode)
        return;

    foreach(TCurveNodeView* nodeView, m_nodeViews) {

        QPointF nodePos(nodeView->scenePos().x(), nodeView->scenePos().y());

        qreal nodeDist = (pos - nodePos).manhattanLength();
        qreal blinkNodeDist = (pos - QPointF(m_blinkingNode->scenePos().x(), m_blinkingNode->scenePos().y())).manhattanLength();

        if (nodeDist < blinkNodeDist) {
            m_blinkingNode = nodeView;
        }
    }

    if ((pos - QPointF(4, 4) - QPointF(m_blinkingNode->scenePos().x(), m_blinkingNode->scenePos().y())).manhattanLength() > NODE_SOFT_SELECTION_DISTANCE) {
        m_blinkingNode = nullptr;
    }

    if (prevNode && (prevNode != m_blinkingNode) ) {
        prevNode->set_color(themer()->get_color("CurveNode:default"));
        prevNode->update();
        prevNode->set_soft_selected(false);
        if (m_blinkingNode) {
            m_blinkingNode->set_soft_selected(true);
        }
    }
    if (!prevNode && m_blinkingNode) {
        m_blinkingNode->set_soft_selected(true);
        m_blinkDarkness = 100;
    }
}


void TCurveView::update_blink_color()
{
    if (!m_blinkingNode) {
        return;
    }

    m_blinkDarkness += (6 * m_blinkColorDirection);

    if (m_blinkDarkness >= 100) {
        m_blinkColorDirection *= -1;
        m_blinkDarkness = 100;
    } else if (m_blinkDarkness <= 40) {
        m_blinkColorDirection *= -1;
        m_blinkDarkness = 40;
    }

    QColor blinkColor = themer()->get_color("CurveNode:blink");

    m_blinkingNode->set_color(blinkColor.lighter(m_blinkDarkness));

    m_blinkingNode->update();
}


TCommand* TCurveView::add_node()
{
    PENTER;
    QPointF point = mapFromScene(cpointer().scene_pos());

    emit curveModified();

    double when = point.x() * double(m_sv->timeref_scalefactor) + m_startoffset.universal_frame();
    double value = (m_boundingRect.height() - point.y()) / m_boundingRect.height();

    TCurveNode* node = new TCurveNode(m_curve, when, value);

    return m_curve->add_node(node);
}


TCommand* TCurveView::remove_node()
{
    PENTER;

    update_softselected_node(cpointer().on_first_input_event_scene_pos());

    QList<TCurveNodeView*> nodesToBeRemoved = get_selected_nodes();


    if (!nodesToBeRemoved.size()) {
        return ied().failure();
    }

    emit curveModified();

    QString description = tr("Removed %n Node(s)", "", nodesToBeRemoved.size());
    QList<TCurveNode*> nodes;

    foreach(TCurveNodeView* nodeView, nodesToBeRemoved) {
        nodeView->set_hard_selected(false);
        nodes.append(nodeView->get_curve_node());
    }

    return m_curve->remove_nodes(nodes, description);
}

TCommand* TCurveView::drag_node()
{
    PENTER;

    update_softselected_node(cpointer().on_first_input_event_scene_pos());

    QList<TCurveNodeView*> selectedNodeViews = get_selected_nodes();
    QList<TCurveNode*> selectedNodes;
    foreach(TCurveNodeView* nodeView, selectedNodeViews) {
        selectedNodes.append(nodeView->get_curve_node());
    }

    if (!selectedNodes.size()) {
        return ied().failure();
    }

    TTimeRef min = TTimeRef();
    TTimeRef max = TTimeRef::max_length();
    const QList<TCurveNode*>& nodeList = m_curve->get_nodes();

    int indexFirstNode = nodeList.indexOf(selectedNodes.first());
    int indexLastNode = nodeList.indexOf(selectedNodes.last());

    if (indexFirstNode > 0) {
        min = TTimeRef((nodeList.at(indexFirstNode-1))->get_when() + 1);
    }

    if (nodeList.size() > (indexLastNode + 1)) {
        max = TTimeRef((nodeList.at(indexLastNode+1))->get_when() - 1);
    }

    // The nodes are in source time, the clip's length is relative to its start
    const TTimeRef clipEnd = TTimeRef(boundingRect().width() * m_sv->timeref_scalefactor) + m_startoffset;
    if (clipEnd < max) {
        max = clipEnd;
    }

    if ((min - get_start_offset()) < TTimeRef()) {
        min = get_start_offset();
    }

    TTimeRef startLocation = TTimeRef(selectedNodes.first()->get_when());
    TTimeRef endLocation  = TTimeRef(selectedNodes.last()->get_when());
    TTimeRef minWhenDiff = min - startLocation;
    TTimeRef maxWhenDiff = max - endLocation;


    double maxValue = DBL_MIN;
    double minValue = DBL_MAX;
    foreach(TCurveNode* node, selectedNodes) {
        double value = node->get_value();
        if (value > maxValue) {
            maxValue = value;
        }
        if (value < minValue) {
            minValue = value;
        }
    }

    double minValueDiff = 0 - minValue;
    double maxValuediff = 1 - maxValue;

    QString text = tr("Move Curve Node(s)", "", selectedNodes.size());

    emit curveModified();

    return new MoveCurveNode(m_curve, selectedNodes, boundingRect().height(), m_sv->timeref_scalefactor,
                             minWhenDiff, maxWhenDiff, minValueDiff, maxValuediff, text);
}

void TCurveView::node_moved( )
{
    TCurveNodeView* prev = nullptr;
    TCurveNodeView* next = nullptr;

    QList<TCurveNodeView*> selectedNodes = get_selected_nodes();

    if (!selectedNodes.size()) {
        // even though there are no selected nodes, a curve node did move
        // e.g. by an undo action, so at least update the view
        update();
        emit curveUpdated(0, int(m_boundingRect.width()));
        return;
    }

    TCurveNodeView* firstSelectedNodeView = selectedNodes.first();
    TCurveNodeView* lastSelectedNodeView = selectedNodes.last();

    int xleft = (int) firstSelectedNodeView->x();
    int xright = (int) lastSelectedNodeView->x();
    int leftindex = m_nodeViews.indexOf(firstSelectedNodeView);
    int rightindex = m_nodeViews.indexOf(lastSelectedNodeView);
    int count = 0;


    if (firstSelectedNodeView == m_nodeViews.first()) {
        xleft = 0;
    } else {
        while ( leftindex > 0 && count < 2) {
            leftindex--;
            count++;
        }
        prev = m_nodeViews.at(leftindex);
    }


    count = 0;

    if (lastSelectedNodeView == m_nodeViews.last()) {
        xright = (int) m_boundingRect.width();
    } else {
        while (rightindex < (m_nodeViews.size() - 1) && count < 2) {
            rightindex++;
            count++;
        }
        next = m_nodeViews.at(rightindex);
    }


    if (prev) xleft = (int) prev->x();
    if (next) xright = (int) next->x();


    update(xleft, 0, xright - xleft + 3, m_boundingRect.height());
    emit curveUpdated(xleft, xright);
}

void TCurveView::load_theme_data()
{
    TCurveView::calculate_bounding_rect();
}

void TCurveView::set_start_offset(const TTimeRef &offset)
{
    m_startoffset = offset;
}

bool TCurveView::has_nodes() const
{
    return m_nodeViews.size() > 1;
}

float TCurveView::get_default_value()
{
    if (m_nodeViews.isEmpty()) {
        return 1.0f;
    }

    return float(m_nodeViews.first()->get_value());
}

TCommand * TCurveView::remove_all_nodes()
{
    return m_curve->remove_nodes(m_curve->get_nodes(), tr("Clear Nodes"));
}

TCommand* TCurveView::select_lazy_selected_node()
{
    if (!m_blinkingNode)
    {
        return ied().failure();
    }

    m_blinkingNode->set_hard_selected(!m_blinkingNode->is_hard_selected());

    return ied().succes();
}

TCommand* TCurveView::toggle_select_all_nodes()
{
    bool selectedNodes = false;
    foreach(TCurveNodeView* nodeView, m_nodeViews) {
        if (nodeView->is_hard_selected()) {
            selectedNodes = true;
            break;
        }
    }

    if (selectedNodes) {
        foreach(TCurveNodeView* nodeView, m_nodeViews) {
            nodeView->set_hard_selected(false);
        }
    } else {
        foreach(TCurveNodeView* nodeView, m_nodeViews) {
            nodeView->set_hard_selected(true);
        }
    }

    update();

    return ied().succes();
}

TCurveNodeView* TCurveView::get_node_view_before(TTimeRef location) const
{
    TTimeRef curveStartOffset = m_curve->get_start_offset();

    for (int i = m_nodeViews.size() - 1; i>=0; --i) {
        TCurveNodeView* nodeview = m_nodeViews.at(i);
        TTimeRef absoluteLocation = TTimeRef(nodeview->get_curve_node()->get_when()) + curveStartOffset;
        if (absoluteLocation < location) {
            return nodeview;
        }
    }

    return nullptr;
}

TCurveNodeView* TCurveView::get_node_view_after(TTimeRef location) const
{
    TTimeRef curveStartOffset = m_curve->get_start_offset();

    foreach(TCurveNodeView* nodeview, m_nodeViews) {
        TTimeRef absoluteLocation = TTimeRef(nodeview->get_curve_node()->get_when()) + curveStartOffset;
        if (absoluteLocation > location) {
            return nodeview;
        }
    }

    return nullptr;
}

QString TCurveView::get_name() const
{
    return "Gain Envelope";
}

QList<TCurveNodeView*> TCurveView::get_selected_nodes()
{
    QList<TCurveNodeView*> list;

    foreach(TCurveNodeView* curveNodeView, m_nodeViews) {
        if (curveNodeView->is_hard_selected()) {
            list.append(curveNodeView);
        }
    }

    if (list.empty() && m_blinkingNode) {
        list.append(m_blinkingNode);

    }

    return list;
}
