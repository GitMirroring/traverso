#ifndef TAUDIODEVICESETUP_H
#define TAUDIODEVICESETUP_H

#include <QList>

// #include "TAudioBusConfiguration.h"
// #include "TAudioChannelConfiguration.h"

#include "defines.h"

class AudioChannel;

class TAudioDeviceSetup {

public:

    TAudioDeviceSetup();

    int set_sample_rate(uint sampleRate);
    int set_buffer_size(nframes_t bufferSize);
    int set_driver_type(const QString &driverType);
    int set_card_device(const QString &cardDevice);
    int set_dither_shape(const QString& ditherShape);

    void set_capture(bool capture);
    void set_playback(bool playback);

    uint get_sample_rate() const {return m_sampleRate;}
    uint get_buffer_size() const {return m_bufferSize;}
    QString get_driver_type() const {return m_driverType;}
    bool get_capture() const {return m_capture;}
    bool get_playback() const {return m_playback;}
    QString get_card_device() const {return m_cardDevice;}
    QString get_dither_shape() const {return m_ditherShape;}

    QList<AudioChannel*> get_jack_channels() const {return m_jackChannels;}

    static QList<uint>	get_buffer_sizes_list();
    static QList<uint> get_sample_rates_list();

private:

    // QList<TAudioBusConfiguration>       busConfigs;
    // QList<TAudioChannelConfiguration>   channelConfigs;
    QList<AudioChannel*>                    m_jackChannels;

    uint            m_sampleRate;
    nframes_t       m_bufferSize;
    QString         m_driverType;
    bool            m_capture;
    bool            m_playback;
    QString         m_cardDevice;
    QString         m_ditherShape;
};

#endif // TAUDIODEVICESETUP_H
