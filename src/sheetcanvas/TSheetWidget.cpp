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

		
#include "TSheetWidget.h"
#include "TTrackPanelViewPort.h"
#include "TClipsViewPort.h"
#include "TTimeLineRulerViewPort.h"
#include "TSheetView.h"
#include "TThemer.h"
#include <algorithm>
#include <cmath>

static constexpr int ZoomSliderSteps = 1000;

#include <TSheet.h>
#include "TContextPointer.h"

#include <QGridLayout>
#include <QScrollBar>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>

#include <Debugger.h>

SheetPanelView::SheetPanelView(QGraphicsScene* scene, TSession* sheet)
	: TViewItem(nullptr, nullptr)
	, m_sheet(sheet)
{
        scene->addItem(this);
        m_boundingRect = QRectF(0, 0, 230, TIMELINE_HEIGHT);
}


void SheetPanelView::paint(QPainter * painter, const QStyleOptionGraphicsItem *  /*option*/, QWidget *  /*widget*/)
{
        painter->fillRect(-3, 0, 3, -TIMELINE_HEIGHT - 1, themer()->get_color("TrackPanel:trackseparation"));
}

SheetPanelViewPort::SheetPanelViewPort(QGraphicsScene * scene, TSheetWidget * sw)
	: TViewPort(scene, sw)
{
    setSceneRect(-230, -TIMELINE_HEIGHT, 230, 0);
    setFixedSize(230, TIMELINE_HEIGHT);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_spv = new SheetPanelView(scene, sw->get_session());
    load_theme();

    // QHBoxLayout* m_mainLayout = new QHBoxLayout(this);

    // m_mainLayout->addWidget(new TTimeLabel(this, sw->get_session()));
    // setLayout(m_mainLayout);

    connect(themer(), &TThemer::themeLoaded, this, &SheetPanelViewPort::load_theme);
}

void SheetPanelViewPort::load_theme()
{
        setBackgroundBrush(themer()->get_color("Timeline:background"));
}

TTimeLabel::TTimeLabel(QWidget* parent, TSession *session)
        : QPushButton(parent)
        , m_session(session)
{
    setEnabled(false);

    setMaximumWidth(120);
    setMinimumHeight(22);
    setFocusPolicy(Qt::NoFocus);
    // setStyleSheet(  "color: darkorange;"
    //               "background-color: black;"
    //               "font: 17px;"
    //               "border-radius: 5px;");

    setText(TTimeRef::timeref_to_ms_2(m_session->get_transport_location()));

    connect(m_session, &TSession::transportLocationChanged, this, [this]() {
        setText(TTimeRef::timeref_to_ms_2(m_session->get_transport_location()));
    });
}


TSheetWidget::TSheetWidget(TSession* sheet, TFrameClock* frameClock, QWidget* parent)
    : QFrame(parent)
	, m_session(sheet)
{
	if (!m_session) {
		return;
	}
    m_scene = new QGraphicsScene();
	m_vScrollBar = new QScrollBar(this);
	m_hScrollBar = new QScrollBar(this);
	m_hScrollBar->setOrientation(Qt::Horizontal);

	m_trackPanel = new TTrackPanelViewPort(m_scene, this);
    m_clipsViewPort = new TClipsViewPort(m_scene, this);
	m_timeLine = new TTimeLineRulerViewPort(m_scene, this);
	m_sheetPanelVP = new SheetPanelViewPort(m_scene, this);


        QWidget* zoomWidget = new QWidget(this);
        zoomWidget->setMaximumWidth(200);
        QHBoxLayout* zoomLayout = new QHBoxLayout(zoomWidget);
        QLabel* zoomLabel = new QLabel(this);
        zoomLabel->setText("Zoom");
        m_zoomSlider = new QSlider(this);
        // Logarithmic between the zoom limits, see zoom_slider_value_changed()
        m_zoomSlider->setRange(0, ZoomSliderSteps);
        m_zoomSlider->setPageStep(ZoomSliderSteps / 20);
        m_zoomSlider->setSingleStep(ZoomSliderSteps / 100);
        m_zoomSlider->setOrientation(Qt::Horizontal);
        sheet_zoom_level_changed();

        connect(m_zoomSlider, &QSlider::sliderMoved, this, &TSheetWidget::zoom_slider_value_changed);
        connect(m_session, &TSession::hzoomChanged, this, &TSheetWidget::sheet_zoom_level_changed);

        zoomLayout->addWidget(zoomLabel);
        zoomLayout->addWidget(m_zoomSlider);

        m_mainLayout = new QGridLayout(this);
        m_mainLayout->addWidget(m_sheetPanelVP, 0, 0);
        m_mainLayout->addWidget(zoomWidget, 2, 0);
	m_mainLayout->addWidget(m_timeLine, 0, 1);
	m_mainLayout->addWidget(m_trackPanel, 1, 0);
	m_mainLayout->addWidget(m_clipsViewPort, 1, 1);
	m_mainLayout->addWidget(m_hScrollBar, 2, 1);
	m_mainLayout->addWidget(m_vScrollBar, 1, 2);
	
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
	m_mainLayout->setSpacing(0);

	setLayout(m_mainLayout);

	m_sv = new TSheetView(this, m_clipsViewPort, m_trackPanel, m_timeLine, sheet, frameClock);
        m_clipsViewPort->set_sheetview(m_sv);
        m_trackPanel->set_sheetview(m_sv);
	m_timeLine->set_sheetview(m_sv);
	m_sheetPanelVP->set_sheet_view(m_sv);
	
	connect(m_clipsViewPort->horizontalScrollBar(), 
		&QScrollBar::valueChanged,
		m_timeLine->horizontalScrollBar(), 
		&QScrollBar::setValue);
	
	connect(m_timeLine->horizontalScrollBar(), 
		&QScrollBar::valueChanged,
		m_clipsViewPort->horizontalScrollBar(), 
		&QScrollBar::setValue);
	
	connect(m_clipsViewPort->verticalScrollBar(), 
		&QScrollBar::valueChanged,
		m_trackPanel->verticalScrollBar(), 
		&QScrollBar::setValue);
	
	connect(m_trackPanel->verticalScrollBar(), 
		&QScrollBar::valueChanged,
		m_clipsViewPort->verticalScrollBar(), 
		&QScrollBar::setValue);
	
    m_timeLine->horizontalScrollBar()->setValue(
            m_clipsViewPort->horizontalScrollBar()->value());

    connect(themer(), &TThemer::themeLoaded, this, &TSheetWidget::load_theme_data, Qt::QueuedConnection);
	
    setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);

	cpointer().set_current_viewport(m_clipsViewPort);
	m_clipsViewPort->setFocus();
}


TSheetWidget::~ TSheetWidget()
{
	PENTERDES;
	if (!m_session) {
		return;
	}

	delete m_scene;
}


QSize TSheetWidget::minimumSizeHint() const
{
	return QSize(400, 200);
}

QSize TSheetWidget::sizeHint() const
{
	return QSize(700, 600);
}

void TSheetWidget::load_theme_data()
{
	QList<QGraphicsItem*> list = m_scene->items();
	
	for (int i = 0; i < list.size(); ++i) {
        TViewItem* item = qgraphicsitem_cast<TViewItem*>(list.at(i));
		if (item) {
			item->load_theme_data();
		}
	}
	
}

TSheet* TSheetWidget::get_sheet() const
{
    return qobject_cast<TSheet*>(m_session);
}

TSession *TSheetWidget::get_session() const
{
    return m_session;
}

TSheetView * TSheetWidget::get_sheetview() const
{
	return m_sv;
}

void TSheetWidget::zoom_slider_value_changed(int value)
{
    if (!m_sv) {
        return;
    }
    const qreal minimum = m_session->minimum_hzoom();
    const qreal maximum = std::max(m_sv->zoom_out_limit(), minimum * 2.0);
    const qreal zoom = minimum * std::pow(maximum / minimum, qreal(value) / ZoomSliderSteps);
    m_sv->hzoom(zoom / m_session->get_hzoom());
}

void TSheetWidget::sheet_zoom_level_changed()
{
    const qreal minimum = m_session->minimum_hzoom();
    // The slider is synced once before the view exists
    const qreal maximum = std::max(m_sv ? m_sv->zoom_out_limit() : m_session->maximum_hzoom(), minimum * 2.0);
    const qreal position = std::log(m_session->get_hzoom() / minimum) / std::log(maximum / minimum);
    // setValue() does not emit sliderMoved, so this does not zoom back
    m_zoomSlider->setValue(std::clamp(qRound(position * ZoomSliderSteps), 0, ZoomSliderSteps));
}
