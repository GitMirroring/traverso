/*
    Copyright (C) 2006-2019 Remon Sijrier

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

#include "PluginChainView.h"

#include <QScrollBar>

#include "TSheetView.h"
#include "ClipsViewPort.h"
#include "TAudioPluginView.h"
#include "TAudioPluginChain.h"
#include "TAudioPlugin.h"

#include "Debugger.h"


PluginChainView::PluginChainView(TSheetView* sv, ViewItem* parent, TAudioPluginChain* chain)
    : ViewItem(parent, parent)
    , m_pluginchain(chain)
{
    PENTERCONS;

    setZValue(parent->zValue() + 10);
    m_sv = sv;
    PluginChainView::calculate_bounding_rect();

    for(auto plugin : chain->get_plugins()) {
        add_plugin(plugin);
    }

    connect(chain, SIGNAL(pluginAdded(TAudioPlugin*)), this, SLOT(add_plugin(TAudioPlugin*)));
    connect(chain, SIGNAL(pluginRemoved(TAudioPlugin*)), this, SLOT(remove_plugin(TAudioPlugin*)));
    connect(m_sv->get_clips_viewport()->horizontalScrollBar(), SIGNAL(valueChanged(int)),
            this, SLOT(scrollbar_value_changed(int)));
}

PluginChainView::~PluginChainView( )
{
    PENTERDES2;
}

void PluginChainView::add_plugin( TAudioPlugin * plugin )
{
    TAudioPluginView* view = new TAudioPluginView(this, m_pluginchain, plugin, m_pluginViews.size());

    int x = 6;
    foreach(TAudioPluginView* view, m_pluginViews) {
        x += int(view->boundingRect().width()) + 6;
    }

    view->setPos(x, m_boundingRect.height() - view->boundingRect().height());

    m_pluginViews.append(view);

    if (m_pluginViews.size() > 1) {
        parentItem()->show();
    }
}

void PluginChainView::remove_plugin( TAudioPlugin * plugin )
{
    foreach(TAudioPluginView* view, m_pluginViews) {
        if (view->get_plugin() == plugin) {
            m_pluginViews.removeAll(view);
            delete view;
        }
    }

    for (int i=0; i<m_pluginViews.size(); ++i) {
        m_pluginViews.at(i)->set_index(i);
    }

    int x = 6;
    foreach(TAudioPluginView* view, m_pluginViews) {
        view->setPos(x, m_boundingRect.height() - view->boundingRect().height());
        x += int(view->boundingRect().width()) + 6;
    }

    if (m_pluginViews.size() == 1) {
        parentItem()->hide();
    }

    m_parentViewItem->update();
}

void PluginChainView::paint( QPainter * painter, const QStyleOptionGraphicsItem * option, QWidget * widget )
{
    Q_UNUSED(painter);
    Q_UNUSED(option);
    Q_UNUSED(widget);
}

void PluginChainView::scrollbar_value_changed(int value)
{
    setPos(value, y());
}

void PluginChainView::calculate_bounding_rect()
{
    m_boundingRect = m_parentViewItem->boundingRect();
    setPos(pos().x(), - 1);
    ViewItem::calculate_bounding_rect();
}

