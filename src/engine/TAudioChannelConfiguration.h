#ifndef TAUDIOCHANNELCONFIGURATION_H
#define TAUDIOCHANNELCONFIGURATION_H

#include <QString>

class TAudioChannelConfiguration
{
public:
    TAudioChannelConfiguration();

    QString name;
    QString type;
    QString destination;
};

#endif // TAUDIOCHANNELCONFIGURATION_H
