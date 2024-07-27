#include "TAudioDeviceSetup.h"

QList<uint>	sampleRatesList;
QList<uint>	periodBufferSizesList;

TAudioDeviceSetup::TAudioDeviceSetup() {
    // set decent values
    m_sampleRate = 44100;
    m_bufferSize = 1024;
    m_driverType = "default";
    m_playback = m_capture = true;
    m_cardDevice = "";
    m_ditherShape = "None";
}

int TAudioDeviceSetup::set_sample_rate(uint sampleRate)
{
    int ret = 1;

    if (!get_sample_rates_list().contains(sampleRate)) {
        printf("TAudioDeviceSetup::set_sample_rate: Unsupported sample rate: %d, resetting to 44100\n", sampleRate);
        sampleRate = 44100;
        ret = -1;
    }

    m_sampleRate = sampleRate;
    return ret;
}

int TAudioDeviceSetup::set_buffer_size(nframes_t bufferSize)
{
    int ret = 1;
    if(!get_buffer_sizes_list().contains(bufferSize)) {
        printf("TAudioDeviceSetup::set_buffer_size: Unsupported buffer size: %d, resetting to 1024\n", bufferSize);
        bufferSize = 1024;
        ret = -1;
    }

    m_bufferSize = bufferSize;
    return ret;
}

int TAudioDeviceSetup::set_driver_type(const QString &driverType)
{
    m_driverType = driverType;
    return 1;
}

void TAudioDeviceSetup::set_capture(bool capture)
{
    m_capture = capture;

}

void TAudioDeviceSetup::set_playback(bool playback)
{
    m_playback = playback;
}

int TAudioDeviceSetup::set_card_device(const QString &cardDevice)
{
    m_cardDevice = cardDevice;
    return 1;
}

int TAudioDeviceSetup::set_dither_shape(const QString &ditherShape)
{
    m_ditherShape = ditherShape;
    return 1;
}

QList<uint> TAudioDeviceSetup::get_buffer_sizes_list()
{
    if (periodBufferSizesList.isEmpty()) {
        periodBufferSizesList << 32 << 64 << 128 << 256 << 512 << 1024 << 2048 << 4096;
    }

    return periodBufferSizesList;
}

QList<uint> TAudioDeviceSetup::get_sample_rates_list()
{
    if (sampleRatesList.isEmpty()) {
        sampleRatesList << 8000 << 11025 << 22050 << 32000 << 44100 << 48000 << 88200 << 9600;
    }

    return sampleRatesList;
}
