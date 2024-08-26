#ifndef CORRELATIONMETERVIEW_H
#define CORRELATIONMETERVIEW_H

#include "widgets/MeterView.h"

class CorrelationMeterWidget;

class CorrelationMeterView : public MeterView
{
	Q_OBJECT

public:
        CorrelationMeterView(CorrelationMeterWidget* widget);

	void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget);

private:
    qreal		coeff;
    qreal		direction;
	int		range;
	QBrush		m_bgBrush;
	QLinearGradient	gradPhase;

	void save_configuration();
	void load_configuration();

private slots:
	void		update_data();
	void		load_theme_data();

public slots:
	TCommand*	set_mode();

};

#endif // CORRELATIONMETERVIEW_H
