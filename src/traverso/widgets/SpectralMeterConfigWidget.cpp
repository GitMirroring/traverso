#include <qwidget.h>

#include "SpectralMeterConfigWidget.h"

#include "TConfig.h"

SpectralMeterConfigWidget::SpectralMeterConfigWidget( QWidget * parent )
    : QDialog(parent)
{
    setupUi(this);
    groupBoxAdvanced->hide();

    load_configuration();

    connect(buttonAdvanced, &QCheckBox::toggled, this, &SpectralMeterConfigWidget::advancedButton_toggled);
}

void SpectralMeterConfigWidget::on_buttonApply_clicked()
{
    save_configuration();
    emit configChanged();
}

void SpectralMeterConfigWidget::on_buttonClose_clicked( )
{
    hide();
}

void SpectralMeterConfigWidget::advancedButton_toggled(bool b)
{
    if (b) {
        groupBoxAdvanced->show();
    } else {
        groupBoxAdvanced->hide();
    }
}

void SpectralMeterConfigWidget::save_configuration( )
{
    config().set_property(	"SpectralMeter",
                          "UpperFrequenty",
                          qMax(spinBoxLowerFreq->value(), spinBoxUpperFreq->value()) );
    config().set_property(	"SpectralMeter",
                          "LowerFrequenty",
                          qMin(spinBoxLowerFreq->value(), spinBoxUpperFreq->value()) );
    config().set_property(	"SpectralMeter",
                          "UpperdB",
                          qMax(spinBoxUpperDb->value(), spinBoxLowerDb->value()) );
    config().set_property(	"SpectralMeter",
                          "LowerdB",
                          qMin(spinBoxUpperDb->value(), spinBoxLowerDb->value()) );
    config().set_property("SpectralMeter", "NumberOfBands", spinBoxNumBands->value() );
    config().set_property("SpectralMeter", "ShowAvarage", checkBoxAverage->isChecked() );

    config().set_property("SpectralMeter", "FFTSize", comboBoxFftSize->currentText().toInt() );
    config().set_property("SpectralMeter", "WindowingFunction", comboBoxWindowing->currentIndex() );
}

void SpectralMeterConfigWidget::load_configuration( )
{
    int value;
    value = config().get_property("SpectralMeter", "UpperFrequenty", 22050).toInt();
    spinBoxUpperFreq->setValue(value);
    value = config().get_property("SpectralMeter", "LowerFrequenty", 20).toInt();
    spinBoxLowerFreq->setValue(value);
    value = config().get_property("SpectralMeter", "UpperdB", 0).toInt();
    spinBoxUpperDb->setValue(value);
    value = config().get_property("SpectralMeter", "LowerdB", -90).toInt();
    spinBoxLowerDb->setValue(value);
    value = config().get_property("SpectralMeter", "NumberOfBands", 16).toInt();
    spinBoxNumBands->setValue(value);
    value = config().get_property("SpectralMeter", "ShowAvarage", 0).toInt();
    checkBoxAverage->setChecked(value);
    value = config().get_property("SpectralMeter", "FFTSize", 2048).toInt();
    QString str;
    str = QString("%1").arg(value);
    int idx = comboBoxFftSize->findText(str);
    idx = idx == -1 ? 3 : idx;
    comboBoxFftSize->setCurrentIndex(idx);
    value = config().get_property("SpectralMeter", "WindowingFunction", 1).toInt();
    comboBoxWindowing->setCurrentIndex(value);
}

