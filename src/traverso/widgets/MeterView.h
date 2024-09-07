#ifndef T_METERVIEW_H
#define T_METERVIEW_H

#include "TViewItem.h"

#include <QTimer>

class MeterWidget;
class TAudioPlugin;
class TProject;
class TSession;

class MeterView : public TViewItem
{
	Q_OBJECT

public:
    explicit MeterView(MeterWidget* widget);
    virtual ~MeterView();

    virtual void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) = 0;
	virtual void resize();
	void hide_event();
	void show_event();

	
protected:
	MeterWidget* 	m_widget;
	TAudioPlugin*		m_meter;
	QTimer		timer;
	QTimer		m_delayTimer;
	TProject*	m_project;
        TSession*	m_session;

private slots:
	void		set_project( TProject* );
        virtual void	update_data() {}
	void		transport_started();
	void		transport_stopped();
	void		delay_timeout();
};

#endif // METERVIEW_H
