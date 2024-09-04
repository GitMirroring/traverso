
#include "MeterView.h"
#include "TAudioPlugin.h"
#include "widgets/MeterWidget.h"

#include "TProject.h"
#include "TProjectManager.h"

static const int STOP_DELAY = 6000; // in ms


MeterView::MeterView(MeterWidget* widget)
    : ViewItem(nullptr, nullptr)
	, m_widget(widget)
    , m_meter(nullptr)
    , m_session(nullptr)
{
        m_project = 0;

	// Nicola: Not sure if we need to initialize here, perhaps a 
	// call to resize would suffice ?
	m_boundingRect = QRectF();

	// Connections to core:
    connect(&pm(), SIGNAL(projectLoaded(TProject*)), this, SLOT(set_project(TProject*)));
    connect(&pm(), &TProjectManager::projectLoaded, this, &MeterView::set_project);
    connect(&timer, SIGNAL(timeout()), this, SLOT(update_data()));
	m_delayTimer.setSingleShot(true);
	connect(&m_delayTimer, SIGNAL(timeout()), this, SLOT(delay_timeout()));
}

MeterView::~MeterView()
{
	if (m_meter) {
        delete m_meter;
	}
}

void MeterView::resize()
{
	PENTER;
	
	prepareGeometryChange();
	// Nicola: Make this as large as the MeterWidget
	// by setting the boundingrect.
	m_boundingRect = QRectF(0, 0, m_widget->width(), m_widget->height());
}

void MeterView::set_project(TProject *project)
{
	if (project) {
                m_project = project;
                m_project->add_meter(m_meter);
                connect(m_project, SIGNAL(transportStarted()), this, SLOT(transport_started()));
                connect(m_project, SIGNAL(transportStopped()), this, SLOT(transport_stopped()));
        } else {
		m_project = 0;
		timer.stop();
	}
}

void MeterView::hide_event()
{
        transport_stopped();
}

void MeterView::show_event()
{
//        if (m_project && m_project->is_transport_rolling()) {
                transport_started();
//        }
}

void MeterView::transport_started()
{
	timer.start(40);
	m_delayTimer.stop();
}

void MeterView::transport_stopped()
{
	m_delayTimer.start(STOP_DELAY);
}

void MeterView::delay_timeout()
{
	timer.stop();
}
