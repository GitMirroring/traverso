SET(TRAVERSO_ENGINE_SOURCES
    AudioBus.cpp
    AudioChannel.cpp
    TAudioDeviceSetup.cpp
    TAudioDevice.cpp
    TAudioDeviceThread.cpp
    TAudioDeviceClient.cpp
    TAudioDriver.cpp
    TAudioBusConfiguration.cpp
    TAudioChannelConfiguration.cpp
    TVUMonitor.cpp
    memops.cpp
)

SET(TRAVERSO_ENGINE_LIBRARY "traversoaudiobackend")
ADD_LIBRARY(${TRAVERSO_ENGINE_LIBRARY} STATIC ${TRAVERSO_ENGINE_SOURCES})

target_include_directories(traversoaudiobackend
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/common
)

target_link_libraries(traversoaudiobackend
    PUBLIC
        PkgConfig::SNDFILE
    PRIVATE
        Qt6::Core
        Qt6::Gui
        traversocore
)

IF(ALSA_FOUND)
    target_sources(traversoaudiobackend PRIVATE TAlsaDriver.cpp)
    target_link_libraries(traversoaudiobackend PUBLIC PkgConfig::ALSA)
ENDIF()

IF(JACK_FOUND)
    target_sources(traversoaudiobackend PRIVATE TJackDriver.cpp)
    target_link_libraries(traversoaudiobackend PUBLIC PkgConfig::JACK)
ENDIF()

IF(PIPEWIRE_FOUND)
    target_sources(traversoaudiobackend PRIVATE TPipeWireDriver.cpp)
    target_link_libraries(traversoaudiobackend PUBLIC PkgConfig::PIPEWIRE)
ENDIF()

IF(PORTAUDIO_FOUND)
    target_sources(traversoaudiobackend PRIVATE TPortAudioDriver.cpp)
    target_link_libraries(traversoaudiobackend PUBLIC PkgConfig::PORTAUDIO)
ENDIF()

IF(WANT_COREAUDIO AND APPLE)
    target_sources(traversoaudiobackend PRIVATE TCoreAudioDriver.cpp)
    target_link_libraries(traversoaudiobackend PUBLIC "-framework CoreAudio" "-framework AudioToolbox")
ENDIF()

IF(USE_PCH)
    target_precompile_headers(traversoaudiobackend REUSE_FROM traversocore)
ENDIF()
