# SPDX-FileCopyrightText: 2026 Remon Sijrier
# SPDX-License-Identifier: GPL-2.0-or-later

# The real-time audio runtime: the audio device with its drivers and the
# audio thread whose period is the clock pulse, buses and channels, getting
# changes safely into and out of that thread (message queue, transactions,
# published snapshots) and watching it (watchdog, xruns, VU meters).
set(TRAVERSO_AUDIORUNTIME_SOURCES
    AudioBus.cpp
    AudioChannel.cpp
    TAudioDeviceSetup.cpp
    TAudioDevice.cpp
    TAudioDeviceThread.cpp
    TAudioThreadWatchdog.h
    TAudioDeviceClient.cpp
    TAudioDriver.cpp
    TAudioBusConfiguration.cpp
    TAudioChannelConfiguration.cpp
    TVUMonitor.cpp
    memops.cpp
    TAudioThreadMessageQueue.h TAudioThreadMessageQueue.cpp
    TTransportControl.h TTransportControl.cpp
    TProcessCallBackData.h
    TRtTransaction.h TRtTransaction.cpp
    TRtValue.h
    TRtList.h
    TRtTransportPosition.h
    TTransportPositionEstimator.h
)

add_library(traversoaudioruntime STATIC ${TRAVERSO_AUDIORUNTIME_SOURCES})

target_include_directories(traversoaudioruntime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(traversoaudioruntime
    PRIVATE
        traverso_compiler_settings
        traversosupport
        traversodsp
        Qt6::Core
)

# Platform drivers, depending on which packages were found
if(TARGET PkgConfig::ALSA)
    target_sources(traversoaudioruntime PRIVATE TAlsaDriver.cpp)
    target_link_libraries(traversoaudioruntime PRIVATE PkgConfig::ALSA)
endif()

if(TARGET PkgConfig::JACK)
    target_sources(traversoaudioruntime PRIVATE TJackDriver.cpp)
    target_link_libraries(traversoaudioruntime PRIVATE PkgConfig::JACK)
endif()

if(TARGET PkgConfig::PIPEWIRE)
    target_sources(traversoaudioruntime PRIVATE TPipeWireDriver.cpp)
    target_link_libraries(traversoaudioruntime PRIVATE PkgConfig::PIPEWIRE)
endif()

if(TARGET PkgConfig::PORTAUDIO)
    target_sources(traversoaudioruntime PRIVATE TPortAudioDriver.cpp)
    target_link_libraries(traversoaudioruntime PRIVATE PkgConfig::PORTAUDIO)
endif()

if(WANT_COREAUDIO AND APPLE)
    target_sources(traversoaudioruntime PRIVATE TCoreAudioDriver.cpp)
    target_link_libraries(traversoaudioruntime PRIVATE "-framework CoreAudio" "-framework AudioToolbox")
endif()
