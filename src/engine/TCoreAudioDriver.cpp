/*
Copyright (C) 2026 Ben Levitt

This file is part of Traverso

Traverso is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA.

*/

#include "TCoreAudioDriver.h"

#include "AudioChannel.h"
#include "TAudioDevice.h"
#include "TTimeRef.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <QUrl>

namespace {

OSStatus property_data(AudioObjectID object, AudioObjectPropertySelector selector,
                       AudioObjectPropertyScope scope, void* value, UInt32* size)
{
    AudioObjectPropertyAddress address{selector, scope, kAudioObjectPropertyElementMain};
    return AudioObjectGetPropertyData(object, &address, 0, nullptr, size, value);
}

OSStatus channel_count(AudioDeviceID device, AudioObjectPropertyScope scope, UInt32* count)
{
    AudioObjectPropertyAddress address{kAudioDevicePropertyStreamConfiguration, scope,
                                       kAudioObjectPropertyElementMain};
    UInt32 size = 0;
    OSStatus status = AudioObjectGetPropertyDataSize(device, &address, 0, nullptr, &size);
    if (status != noErr) {
        return status;
    }

    std::unique_ptr<AudioBufferList, decltype(&std::free)> buffers(
        static_cast<AudioBufferList*>(std::malloc(size)), &std::free);
    if (!buffers) {
        return kAudioHardwareUnspecifiedError;
    }
    status = AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, buffers.get());
    if (status != noErr) {
        return status;
    }

    *count = 0;
    for (UInt32 index = 0; index < buffers->mNumberBuffers; ++index) {
        *count += buffers->mBuffers[index].mNumberChannels;
    }
    return noErr;
}

AudioDeviceID default_device(bool captureOnly)
{
    AudioDeviceID device = kAudioDeviceUnknown;
    UInt32 size = sizeof(device);
    property_data(kAudioObjectSystemObject,
                  captureOnly ? kAudioHardwarePropertyDefaultInputDevice
                              : kAudioHardwarePropertyDefaultOutputDevice,
                  kAudioObjectPropertyScopeGlobal, &device, &size);
    return device;
}

AudioDeviceID duplex_device()
{
    AudioObjectPropertyAddress address{kAudioHardwarePropertyDevices,
                                       kAudioObjectPropertyScopeGlobal,
                                       kAudioObjectPropertyElementMain};
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &address, 0, nullptr, &size) != noErr) {
        return kAudioDeviceUnknown;
    }

    const UInt32 count = size / sizeof(AudioDeviceID);
    std::unique_ptr<AudioDeviceID[]> devices(new AudioDeviceID[count]);
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr, &size,
                                   devices.get()) != noErr) {
        return kAudioDeviceUnknown;
    }

    for (UInt32 index = 0; index < count; ++index) {
        UInt32 inputs = 0;
        UInt32 outputs = 0;
        if (channel_count(devices[index], kAudioObjectPropertyScopeInput, &inputs) == noErr &&
            channel_count(devices[index], kAudioObjectPropertyScopeOutput, &outputs) == noErr &&
            inputs > 0 && outputs > 0) {
            return devices[index];
        }
    }
    return kAudioDeviceUnknown;
}

OSStatus device_for_uid(const QString& uid, AudioDeviceID* device)
{
    const QByteArray uidBytes = uid.toUtf8();
    if (uidBytes.isEmpty() || !device) {
        return kAudioHardwareBadDeviceError;
    }
    CFStringRef value = CFStringCreateWithCString(kCFAllocatorDefault, uidBytes.constData(),
                                                   kCFStringEncodingUTF8);
    if (!value) {
        return kAudioHardwareUnspecifiedError;
    }

    AudioValueTranslation translation{
        &value,
        sizeof(value),
        device,
        sizeof(*device)
    };
    UInt32 size = sizeof(translation);
    AudioObjectPropertyAddress address{kAudioHardwarePropertyDeviceForUID,
                                       kAudioObjectPropertyScopeGlobal,
                                       kAudioObjectPropertyElementMain};
    OSStatus status = AudioObjectGetPropertyData(kAudioObjectSystemObject, &address,
                                                  0, nullptr, &size, &translation);
    CFRelease(value);
    if (status == noErr && *device == kAudioDeviceUnknown) {
        return kAudioHardwareBadDeviceError;
    }
    return status;
}

AudioStreamBasicDescription client_format(double sampleRate, UInt32 channels)
{
    AudioStreamBasicDescription format{};
    format.mSampleRate = sampleRate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagsNativeFloatPacked;
    format.mBytesPerPacket = sizeof(audio_sample_t) * channels;
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = sizeof(audio_sample_t) * channels;
    format.mChannelsPerFrame = channels;
    format.mBitsPerChannel = sizeof(audio_sample_t) * 8;
    return format;
}

QString device_name(AudioDeviceID device)
{
    CFStringRef name = nullptr;
    UInt32 size = sizeof(name);
    if (property_data(device, kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal,
                      &name, &size) != noErr || !name) {
        return QStringLiteral("CoreAudio");
    }

    char buffer[256]{};
    QString result = CFStringGetCString(name, buffer, sizeof(buffer), kCFStringEncodingUTF8)
                         ? QString::fromUtf8(buffer)
                         : QStringLiteral("CoreAudio");
    CFRelease(name);
    return result;
}

QString device_uid(AudioDeviceID device)
{
    CFStringRef uid = nullptr;
    UInt32 size = sizeof(uid);
    if (property_data(device, kAudioDevicePropertyDeviceUID, kAudioObjectPropertyScopeGlobal,
                      &uid, &size) != noErr || !uid) {
        return {};
    }
    char buffer[512]{};
    QString result = CFStringGetCString(uid, buffer, sizeof(buffer), kCFStringEncodingUTF8)
                         ? QString::fromUtf8(buffer)
                         : QString();
    CFRelease(uid);
    return result;
}

} // namespace

TCoreAudioDriver::TCoreAudioDriver(TAudioDevice* device)
    : TAudioDriver(device)
{
    read = TAudioDriverReadWriteCallBack(this, &TCoreAudioDriver::_read);
    write = TAudioDriverReadWriteCallBack(this, &TCoreAudioDriver::_write);
    run_cycle = RunCycleCallback(this, &TCoreAudioDriver::_run_cycle);
}

TCoreAudioDriver::~TCoreAudioDriver()
{
    stop();
    std::free(m_inputBuffer);
    std::free(m_inputList);
    if (m_audioUnit) {
        AudioUnitUninitialize(m_audioUnit);
        AudioComponentInstanceDispose(m_audioUnit);
    }
    if (m_inputAudioUnit) {
        AudioUnitUninitialize(m_inputAudioUnit);
        AudioComponentInstanceDispose(m_inputAudioUnit);
    }
}

int TCoreAudioDriver::fail_setup(const QString& message, OSStatus status)
{
    const QString detail = status == noErr
                               ? message
                               : tr("%1 (CoreAudio status %2)").arg(message).arg(status);
    emit driverSetupMessage(QStringLiteral("CoreAudio"), detail,
                            TAudioDevice::DRIVER_SETUP_FAILURE);
    return -1;
}

int TCoreAudioDriver::setup(bool capture, bool playback, const QString& device)
{
    m_capture = capture;
    m_playback = playback;
    m_frameRate = m_device->get_sample_rate();
    m_framesPerCycle = m_device->get_buffer_size();

    m_deviceId = default_device(capture && !playback);
    m_inputDeviceId = default_device(true);
    const QStringList selectedDevices = device.split(QStringLiteral("::"), Qt::KeepEmptyParts);
    const bool hasSeparateSelections = capture && playback && selectedDevices.size() == 2;
    if (hasSeparateSelections) {
        const QString inputUid = selectedDevices.at(0) == QStringLiteral("default")
                                     ? QStringLiteral("default")
                                     : QUrl::fromPercentEncoding(selectedDevices.at(0).toUtf8());
        const QString outputUid = selectedDevices.at(1) == QStringLiteral("default")
                                      ? QStringLiteral("default")
                                      : QUrl::fromPercentEncoding(selectedDevices.at(1).toUtf8());
        AudioDeviceID selectedInput = m_inputDeviceId;
        AudioDeviceID selectedOutput = m_deviceId;
        if (inputUid != QStringLiteral("default") &&
            device_for_uid(inputUid, &selectedInput) != noErr) {
            return fail_setup(tr("Could not find the selected CoreAudio input device"));
        }
        if (outputUid != QStringLiteral("default") &&
            device_for_uid(outputUid, &selectedOutput) != noErr) {
            return fail_setup(tr("Could not find the selected CoreAudio output device"));
        }
        if (selectedInput == kAudioDeviceUnknown || selectedOutput == kAudioDeviceUnknown) {
            return fail_setup(tr("The selected CoreAudio device is unavailable"));
        }
        m_inputDeviceId = selectedInput;
        m_deviceId = selectedOutput;
    }
    if (!device.isEmpty() && device != QStringLiteral("none") &&
        device != QStringLiteral("default") && !hasSeparateSelections) {
        if (device_for_uid(device, &m_deviceId) != noErr ||
            m_deviceId == kAudioDeviceUnknown) {
            return fail_setup(tr("Could not find CoreAudio device %1").arg(device));
        }
    }
    if (m_deviceId == kAudioDeviceUnknown) {
        return fail_setup(tr("No default CoreAudio device is available"));
    }
    const bool separateDevices = hasSeparateSelections && m_inputDeviceId != m_deviceId;

    AudioObjectPropertyAddress rateAddress{kAudioDevicePropertyNominalSampleRate,
                                           kAudioObjectPropertyScopeGlobal,
                                           kAudioObjectPropertyElementMain};
    Float64 sampleRate = m_frameRate;
    UInt32 rateSize = sizeof(sampleRate);
    OSStatus status = AudioObjectSetPropertyData(m_deviceId, &rateAddress, 0, nullptr,
                                                  rateSize, &sampleRate);
    if (status != noErr) {
        rateSize = sizeof(sampleRate);
        status = AudioObjectGetPropertyData(m_deviceId, &rateAddress, 0, nullptr,
                                            &rateSize, &sampleRate);
        if (status != noErr || sampleRate <= 0.0) {
            return fail_setup(tr("Could not determine the CoreAudio sample rate"), status);
        }
        m_frameRate = static_cast<nframes_t>(sampleRate);
    }

    AudioObjectPropertyAddress bufferAddress{kAudioDevicePropertyBufferFrameSize,
                                             kAudioObjectPropertyScopeGlobal,
                                             kAudioObjectPropertyElementMain};
    UInt32 bufferSize = m_framesPerCycle;
    AudioObjectSetPropertyData(m_deviceId, &bufferAddress, 0, nullptr,
                               sizeof(bufferSize), &bufferSize);
    UInt32 bufferSizeBytes = sizeof(bufferSize);
    status = AudioObjectGetPropertyData(m_deviceId, &bufferAddress, 0, nullptr,
                                        &bufferSizeBytes, &bufferSize);
    if (status != noErr || bufferSize == 0) {
        return fail_setup(tr("Could not determine the CoreAudio buffer size"), status);
    }
    m_framesPerCycle = bufferSize;
    m_device->set_buffer_size(m_framesPerCycle);
    m_device->set_sample_rate(m_frameRate);
    m_periodTimeInMicroSeconds = static_cast<trav_time_t>(
        static_cast<double>(m_framesPerCycle) / m_frameRate * 1000000.0);

    UInt32 inputChannels = 0;
    UInt32 outputChannels = 0;
    if (capture && channel_count(m_inputDeviceId, kAudioObjectPropertyScopeInput,
                                 &inputChannels) != noErr) {
        return fail_setup(tr("Could not query CoreAudio input channels"));
    }
    if (playback && channel_count(m_deviceId, kAudioObjectPropertyScopeOutput,
                                  &outputChannels) != noErr) {
        return fail_setup(tr("Could not query CoreAudio output channels"));
    }

    if (capture && playback && (inputChannels == 0 || outputChannels == 0) &&
        (device.isEmpty() || device == QStringLiteral("none") ||
         device == QStringLiteral("default"))) {
        m_deviceId = duplex_device();
        if (m_deviceId == kAudioDeviceUnknown) {
            return fail_setup(tr("No CoreAudio device supports both input and output"));
        }
        if (channel_count(m_deviceId, kAudioObjectPropertyScopeInput, &inputChannels) != noErr ||
            channel_count(m_deviceId, kAudioObjectPropertyScopeOutput, &outputChannels) != noErr) {
            return fail_setup(tr("Could not query the duplex CoreAudio device"));
        }
    }
    m_inputChannels = inputChannels;
    m_outputChannels = outputChannels;
    if ((capture && m_inputChannels == 0) || (playback && m_outputChannels == 0)) {
        return fail_setup(tr("The selected CoreAudio device does not support the requested mode"));
    }

    AudioComponentDescription description{kAudioUnitType_Output, kAudioUnitSubType_HALOutput,
                                          kAudioUnitManufacturer_Apple, 0, 0};
    AudioComponent component = AudioComponentFindNext(nullptr, &description);
    if (!component) {
        return fail_setup(tr("The CoreAudio HAL component is unavailable"));
    }
    status = AudioComponentInstanceNew(component, &m_audioUnit);
    if (status != noErr) {
        return fail_setup(tr("Could not create the CoreAudio HAL unit"), status);
    }

    UInt32 enabled = capture && !separateDevices ? 1 : 0;
    status = AudioUnitSetProperty(m_audioUnit, kAudioOutputUnitProperty_EnableIO,
                                  kAudioUnitScope_Input, 1, &enabled, sizeof(enabled));
    if (status != noErr) {
        return fail_setup(tr("Could not enable CoreAudio input"), status);
    }
    if (!playback) {
        enabled = 0;
        status = AudioUnitSetProperty(m_audioUnit, kAudioOutputUnitProperty_EnableIO,
                                      kAudioUnitScope_Output, 0, &enabled, sizeof(enabled));
        if (status != noErr) {
            return fail_setup(tr("Could not disable CoreAudio output"), status);
        }
    }

    status = AudioUnitSetProperty(m_audioUnit, kAudioOutputUnitProperty_CurrentDevice,
                                  kAudioUnitScope_Global, 0, &m_deviceId, sizeof(m_deviceId));
    if (status != noErr) {
        return fail_setup(tr("Could not select the CoreAudio device"), status);
    }

    UInt32 maximumFrames = m_framesPerCycle;
    AudioUnitSetProperty(m_audioUnit, kAudioUnitProperty_MaximumFramesPerSlice,
                          kAudioUnitScope_Global, 0, &maximumFrames, sizeof(maximumFrames));
    if (capture && !separateDevices) {
        AudioStreamBasicDescription format = client_format(m_frameRate, m_inputChannels);
        status = AudioUnitSetProperty(m_audioUnit, kAudioUnitProperty_StreamFormat,
                                      kAudioUnitScope_Output, 1, &format, sizeof(format));
        if (status != noErr) {
            return fail_setup(tr("Could not configure CoreAudio input format"), status);
        }
    }

    if (separateDevices) {
        status = AudioComponentInstanceNew(component, &m_inputAudioUnit);
        if (status != noErr) {
            return fail_setup(tr("Could not create the CoreAudio input unit"), status);
        }
        enabled = 1;
        status = AudioUnitSetProperty(m_inputAudioUnit, kAudioOutputUnitProperty_EnableIO,
                                      kAudioUnitScope_Input, 1, &enabled, sizeof(enabled));
        if (status != noErr) {
            return fail_setup(tr("Could not enable the selected CoreAudio input"), status);
        }
        enabled = 0;
        status = AudioUnitSetProperty(m_inputAudioUnit, kAudioOutputUnitProperty_EnableIO,
                                      kAudioUnitScope_Output, 0, &enabled, sizeof(enabled));
        if (status != noErr) {
            return fail_setup(tr("Could not disable CoreAudio input-unit output"), status);
        }
        status = AudioUnitSetProperty(m_inputAudioUnit, kAudioOutputUnitProperty_CurrentDevice,
                                      kAudioUnitScope_Global, 0, &m_inputDeviceId,
                                      sizeof(m_inputDeviceId));
        if (status != noErr) {
            return fail_setup(tr("Could not select the CoreAudio input device"), status);
        }
        AudioStreamBasicDescription inputFormat = client_format(m_frameRate, m_inputChannels);
        status = AudioUnitSetProperty(m_inputAudioUnit, kAudioUnitProperty_StreamFormat,
                                      kAudioUnitScope_Output, 1, &inputFormat,
                                      sizeof(inputFormat));
        if (status != noErr) {
            return fail_setup(tr("Could not configure the selected CoreAudio input"), status);
        }
        status = AudioUnitSetProperty(m_inputAudioUnit,
                                      kAudioUnitProperty_MaximumFramesPerSlice,
                                      kAudioUnitScope_Global, 0, &maximumFrames,
                                      sizeof(maximumFrames));
        if (status != noErr) {
            return fail_setup(tr("Could not configure the CoreAudio input buffer size"), status);
        }
        AURenderCallbackStruct inputCallback{&TCoreAudioDriver::input_render_callback, this};
        status = AudioUnitSetProperty(m_inputAudioUnit, kAudioUnitProperty_SetRenderCallback,
                                      kAudioUnitScope_Global, 0, &inputCallback,
                                      sizeof(inputCallback));
        if (status != noErr) {
            return fail_setup(tr("Could not install the CoreAudio input callback"), status);
        }
    }
    if (playback) {
        AudioStreamBasicDescription format = client_format(m_frameRate, m_outputChannels);
        status = AudioUnitSetProperty(m_audioUnit, kAudioUnitProperty_StreamFormat,
                                      kAudioUnitScope_Input, 0, &format, sizeof(format));
        if (status != noErr) {
            return fail_setup(tr("Could not configure CoreAudio output format"), status);
        }
    }

    AURenderCallbackStruct callback{&TCoreAudioDriver::render_callback, this};
    status = AudioUnitSetProperty(m_audioUnit, kAudioUnitProperty_SetRenderCallback,
                                  kAudioUnitScope_Input, 0, &callback, sizeof(callback));
    if (status != noErr) {
        return fail_setup(tr("Could not install the CoreAudio render callback"), status);
    }
    status = AudioUnitInitialize(m_audioUnit);
    if (status != noErr) {
        return fail_setup(tr("Could not initialize the CoreAudio HAL unit"), status);
    }
    if (separateDevices) {
        status = AudioUnitInitialize(m_inputAudioUnit);
        if (status != noErr) {
            return fail_setup(tr("Could not initialize the CoreAudio input unit"), status);
        }
    }

    const size_t listSize = offsetof(AudioBufferList, mBuffers) + sizeof(AudioBuffer);
    m_inputList = static_cast<AudioBufferList*>(std::calloc(1, listSize));
    if (!m_inputList) {
        return fail_setup(tr("Could not allocate CoreAudio input buffers"));
    }
    m_inputList->mNumberBuffers = capture ? 1 : 0;
    if (capture) {
        m_inputList->mBuffers[0].mNumberChannels = m_inputChannels;
        m_inputList->mBuffers[0].mDataByteSize =
            m_framesPerCycle * m_inputChannels * sizeof(audio_sample_t);
        m_inputBuffer = static_cast<audio_sample_t*>(std::calloc(
            m_framesPerCycle * m_inputChannels, sizeof(audio_sample_t)));
        if (!m_inputBuffer) {
            return fail_setup(tr("Could not allocate CoreAudio capture storage"));
        }
        m_inputList->mBuffers[0].mData = m_inputBuffer;
    }

    for (channel_t index = 0; index < m_inputChannels; ++index) {
        AudioChannel* channel = add_capture_channel(QStringLiteral("capture_%1").arg(index + 1));
        channel->set_latency(m_framesPerCycle + m_captureFrameLatency);
    }
    for (channel_t index = 0; index < m_outputChannels; ++index) {
        AudioChannel* channel = add_playback_channel(QStringLiteral("playback_%1").arg(index + 1));
        channel->set_latency(m_framesPerCycle + m_playbackFrameLatency);
    }

    emit driverSetupMessage(QStringLiteral("CoreAudio"),
                            tr("Connected to %1").arg(device_name(m_deviceId)),
                            TAudioDevice::DRIVER_SETUP_SUCCESS);
    return 1;
}

int TCoreAudioDriver::attach()
{
    m_device->set_buffer_size(m_framesPerCycle);
    m_device->set_sample_rate(m_frameRate);
    return 1;
}

int TCoreAudioDriver::start()
{
    if (!m_audioUnit || AudioOutputUnitStart(m_audioUnit) != noErr) {
        return -1;
    }
    if (m_inputAudioUnit && AudioOutputUnitStart(m_inputAudioUnit) != noErr) {
        AudioOutputUnitStop(m_audioUnit);
        return -1;
    }
    m_running = true;
    return 1;
}

int TCoreAudioDriver::stop()
{
    if (m_audioUnit && m_running) {
        AudioOutputUnitStop(m_audioUnit);
    }
    if (m_inputAudioUnit && m_running) {
        AudioOutputUnitStop(m_inputAudioUnit);
    }
    m_running = false;
    return 1;
}

int TCoreAudioDriver::_read(nframes_t)
{
    return 1;
}

int TCoreAudioDriver::_write(nframes_t)
{
    return 1;
}

int TCoreAudioDriver::process_callback(AudioUnitRenderActionFlags* flags,
                                       const AudioTimeStamp* timestamp,
                                       nframes_t nframes,
                                       AudioBufferList* output)
{
    if (m_capture) {
        if (!m_inputAudioUnit) {
            m_lastInputRenderStatus = AudioUnitRender(m_audioUnit, flags, timestamp, 1,
                                                      nframes, m_inputList);
            if (m_lastInputRenderStatus != noErr) {
                if (m_lastInputRenderStatus != m_reportedInputRenderStatus) {
                    std::fprintf(stderr, "TCoreAudioDriver: AudioUnitRender failed with status %d\n",
                                 m_lastInputRenderStatus);
                    m_reportedInputRenderStatus = m_lastInputRenderStatus;
                }
                m_device->xrun();
                return -1;
            }
        }

        if (m_inputAudioUnit && !m_inputReady.exchange(false, std::memory_order_acquire)) {
            for (AudioChannel* channel : m_captureChannels) {
                channel->silence_buffer();
            }
        } else {
            const auto* input = static_cast<const audio_sample_t*>(m_inputList->mBuffers[0].mData);
            for (channel_t channel = 0; channel < m_inputChannels; ++channel) {
                audio_sample_t* destination = m_captureChannels.at(channel)->get_buffer().get_data(nframes);
                for (nframes_t frame = 0; frame < nframes; ++frame) {
                    destination[frame] = input[frame * m_inputChannels + channel];
                }
            }
        }
    }

    m_runCycleStartTime = TTimeRef::get_nanoseconds_since_epoch();
    m_device->set_transport_cycle_start_time(m_runCycleStartTime);
    if (m_device->run_cycle(nframes, 0) < 0) {
        return -1;
    }

    if (m_playback && output && output->mNumberBuffers == 1 && output->mBuffers[0].mData) {
        auto* destination = static_cast<audio_sample_t*>(output->mBuffers[0].mData);
        for (nframes_t frame = 0; frame < nframes; ++frame) {
            for (channel_t channel = 0; channel < m_outputChannels; ++channel) {
                destination[frame * m_outputChannels + channel] =
                    m_playbackChannels.at(channel)->get_buffer().at(frame);
            }
        }
    }
    for (channel_t channel = 0; channel < m_outputChannels; ++channel) {
        m_playbackChannels.at(channel)->silence_buffer();
    }

    m_runCycleEndTime = TTimeRef::get_nanoseconds_since_epoch();
    m_device->set_transport_cycle_end_time(m_runCycleEndTime);
    return 0;
}

OSStatus TCoreAudioDriver::capture_callback(AudioUnitRenderActionFlags* flags,
                                             const AudioTimeStamp* timestamp,
                                             nframes_t nframes)
{
    m_lastInputRenderStatus = AudioUnitRender(m_inputAudioUnit, flags, timestamp, 1,
                                               nframes, m_inputList);
    if (m_lastInputRenderStatus != noErr) {
        if (m_lastInputRenderStatus != m_reportedInputRenderStatus) {
            std::fprintf(stderr, "TCoreAudioDriver: input AudioUnitRender failed with status %d\n",
                         m_lastInputRenderStatus);
            m_reportedInputRenderStatus = m_lastInputRenderStatus;
        }
        m_device->xrun();
        return m_lastInputRenderStatus;
    }
    m_inputReady.store(true, std::memory_order_release);
    return noErr;
}

OSStatus TCoreAudioDriver::render_callback(void* refCon, AudioUnitRenderActionFlags* flags,
                                           const AudioTimeStamp* timestamp, UInt32, UInt32 frames,
                                           AudioBufferList* output)
{
    return static_cast<TCoreAudioDriver*>(refCon)->process_callback(flags, timestamp, frames, output) == 0
               ? noErr
               : kAudioHardwareUnspecifiedError;
}

OSStatus TCoreAudioDriver::input_render_callback(void* refCon, AudioUnitRenderActionFlags* flags,
                                                 const AudioTimeStamp* timestamp, UInt32,
                                                 UInt32 frames, AudioBufferList*)
{
    return static_cast<TCoreAudioDriver*>(refCon)->capture_callback(flags, timestamp, frames);
}

QString TCoreAudioDriver::get_device_name()
{
    return device_name(m_deviceId);
}

QString TCoreAudioDriver::get_device_longname()
{
    return get_device_name();
}

QStringList TCoreAudioDriver::devices_info(bool input)
{
    QStringList result;
    AudioObjectPropertyAddress address{kAudioHardwarePropertyDevices,
                                       kAudioObjectPropertyScopeGlobal,
                                       kAudioObjectPropertyElementMain};
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &address, 0, nullptr, &size) != noErr) {
        return result;
    }
    const UInt32 count = size / sizeof(AudioDeviceID);
    std::unique_ptr<AudioDeviceID[]> devices(new AudioDeviceID[count]);
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr, &size,
                                   devices.get()) != noErr) {
        return result;
    }
    for (UInt32 index = 0; index < count; ++index) {
        UInt32 channels = 0;
        if (channel_count(devices[index], input ? kAudioObjectPropertyScopeInput
                                                : kAudioObjectPropertyScopeOutput,
                          &channels) == noErr && channels > 0) {
            const QString uid = device_uid(devices[index]);
            if (!uid.isEmpty()) {
                result.append(device_name(devices[index]) + QStringLiteral("###") + uid);
            }
        }
    }
    return result;
}
