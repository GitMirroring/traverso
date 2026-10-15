#ifndef TAUDIOBUSCONFIGURATION_H
#define TAUDIOBUSCONFIGURATION_H

#include <QString>
#include <QStringList>

class TAudioBusConfiguration
{
public:
    TAudioBusConfiguration();

    QString name;
    QStringList channelNames;
    QString type;
    QString bustype;
    int channelcount;
    bool isInternalBus;
    qint64 id;
};

#endif // TAUDIOBUSCONFIGURATION_H
