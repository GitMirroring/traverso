# Accumulate implementation file lists handling physical audio transport processes
set(TRAVERSO_ENGINE_SOURCES
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

# Define the driver abstraction and sound routing engine library
add_library(traversoaudiobackend STATIC ${TRAVERSO_ENGINE_SOURCES})
target_link_libraries(traversoaudiobackend PRIVATE traverso_compiler_settings)

# Expose internal directories and cross-module entry mappings
target_include_directories(traversoaudiobackend
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/common
)

# Establish required upstream structural linkages
target_link_libraries(traversoaudiobackend
    PUBLIC
        Qt6::Core
        Qt6::Gui
        PkgConfig::SNDFILE
    PRIVATE
        traversocore
)

# Process platform drivers contextually based on package scanning outcomes
if(TARGET PkgConfig::ALSA)
    target_sources(traversoaudiobackend PRIVATE TAlsaDriver.cpp)
    target_link_libraries(traversoaudiobackend PUBLIC PkgConfig::ALSA)
endif()

if(TARGET PkgConfig::JACK)
    target_sources(traversoaudiobackend PRIVATE TJackDriver.cpp)
    target_link_libraries(traversoaudiobackend PUBLIC PkgConfig::JACK)
endif()

if(TARGET PkgConfig::PIPEWIRE)
    target_sources(traversoaudiobackend PRIVATE TPipeWireDriver.cpp)
    target_link_libraries(traversoaudiobackend PUBLIC PkgConfig::PIPEWIRE)
endif()

if(TARGET PkgConfig::PORTAUDIO)
    target_sources(traversoaudiobackend PRIVATE TPortAudioDriver.cpp)
    target_link_libraries(traversoaudiobackend PUBLIC PkgConfig::PORTAUDIO)
endif()

if(WANT_COREAUDIO AND APPLE)
    target_sources(traversoaudiobackend PRIVATE TCoreAudioDriver.cpp)
    target_link_libraries(traversoaudiobackend PUBLIC "-framework CoreAudio" "-framework AudioToolbox")
endif()
