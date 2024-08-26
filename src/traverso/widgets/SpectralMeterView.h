#ifndef SPECTRALMETERVIEW_H
#define SPECTRALMETERVIEW_H

#include "MeterView.h"

class SpectralMeterConfigWidget;
class SpectralMeterWidget;

class SpectralMeterView : public MeterView
{
	Q_OBJECT

public:
        SpectralMeterView(SpectralMeterWidget* widget);
	
	void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget);
	virtual void resize();

private:
	QVector<float>	specl;
	QVector<float>	specr;
	QVector<float>	m_spectrum;
	QVector<float>	m_history;
	QVector<float>	m_bands;
	QVector<float>	m_freq_labels;
	QVector<float>	m_avg_db;
	QVector<float>	m_map_idx2xpos;
	QVector<float>	m_map_idx2freq;
	QRect		m_rect;
	SpectralMeterConfigWidget *m_config;
	QPixmap		bgPixmap;
	uint		num_bands;
	uint		sample_rate;
	float		upper_freq;
	float		lower_freq;
	float		upper_db;
	float		lower_db;
	int		margin_l;
	int		margin_r;
	int		margin_t;
	int		margin_b;
	uint		sample_weight;

	uint		fft_size;
	float		xfactor;
	float		upper_freq_log;
	float		lower_freq_log;
	float		freq_step;
	int		bar_offset;
	bool		show_average;
	bool		update_average;

	void		reduce_bands();
	void		update_layout();
	void		update_freq_map();
	float		db2ypos(float);
	float		freq2xpos(float);
	void		update_background();
	float		freq2db(float, float);
	QString		get_xmgr_string();
	QBrush		m_brushBg;
	QBrush		m_brushFg;
	QBrush		m_brushMargin;
	QPen		m_penAvgCurve;
	QPen		m_penTickMain;
	QPen		m_penTickSub;
	QPen		m_penFont;
	QPen		m_penGrid;

private slots:
	void		update_data();
	void		load_theme_data();
    void        audiodevice_params_changed();

public slots:
	void		load_configuration();

	TCommand*	edit_properties();
	TCommand*	set_mode();
	TCommand*	reset();
	TCommand*	export_average_curve();
	TCommand*	screen_capture();
};

#endif // SPECTRALMETERVIEW_H
