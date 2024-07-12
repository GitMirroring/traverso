#ifndef TAUDIODEVICESETUP_H
#define TAUDIODEVICESETUP_H

#include <QList>

#include "TAudioBusConfiguration.h"
#include "TAudioChannelConfiguration.h"

#include "defines.h"

class AudioChannel;

class TAudioDeviceSetup {

public:

    TAudioDeviceSetup();

    QList<TAudioBusConfiguration>       busConfigs;
    QList<TAudioChannelConfiguration>   channelConfigs;
    QList<AudioChannel*>                jackChannels;
    uint            rate;
    nframes_t       bufferSize;
    QString         driverType;
    bool            capture;
    bool            playback;
    QString         cardDevice;
    QString         ditherShape;
};

#endif // TAUDIODEVICESETUP_H
