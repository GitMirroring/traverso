#ifndef SPECTRALMETERCONFIGWIDGET_H
#define SPECTRALMETERCONFIGWIDGET_H

#include "ui_SpectralMeterConfigWidget.h"
#include <qdialog.h>


class SpectralMeterConfigWidget : public QDialog, private Ui::SpectralMeterConfigWidget
{
	Q_OBJECT

public:
	SpectralMeterConfigWidget(QWidget* parent = 0);

private:
	void save_configuration();
	void load_configuration();
	
private slots:
	void on_buttonClose_clicked();
	void on_buttonApply_clicked();
	void advancedButton_toggled(bool);

signals:
	void configChanged();

};


#endif // SPECTRALMETERCONFIGWIDGET_H
