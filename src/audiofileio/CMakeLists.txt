# Define the static library for managing audio file input and output processes
add_library(traversoaudiofileio STATIC
    decode/AbstractAudioReader.cpp
    decode/TAudioDecoder.h decode/TAudioDecoder.cpp
    decode/SFAudioReader.cpp
    decode/TResampleAudioReader.cpp
    decode/WPAudioReader.cpp
    decode/MadAudioReader.cpp
    encode/AbstractAudioWriter.cpp
    encode/SFAudioWriter.cpp
    encode/WPAudioWriter.cpp
    encode/TResampleAudioWriter.cpp
)

# Apply shared compilation variables and parameters from the interface target
target_link_libraries(traversoaudiofileio PRIVATE traverso_compiler_settings)

# Include optional implementation files depending on PkgConfig validation states
if(TARGET PkgConfig::FAAD2)
    target_sources(traversoaudiofileio PRIVATE decode/FaadAudioReader.cpp decode/minimp4.h)
endif()

if(TARGET PkgConfig::FAAC)
    target_sources(traversoaudiofileio PRIVATE encode/FaacAudioWriter.cpp)
endif()

# Specify directory search paths for source compilation and downstream targets
# Only its own directories: it never includes headers of libraries it does
# not link
target_include_directories(traversoaudiofileio
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/decode
        ${CMAKE_CURRENT_SOURCE_DIR}/encode
)

# Direct dependencies only, all private: consumers link what they use
# themselves. Resampling and dithering come from dsp.
target_link_libraries(traversoaudiofileio
    PRIVATE
        traversosupport
        traversodsp
        Qt6::Core
        PkgConfig::SNDFILE
        PkgConfig::WAVPACK
        PkgConfig::SAMPLERATE
        PkgConfig::MAD
)

if(TARGET PkgConfig::FAAD2)
    target_link_libraries(traversoaudiofileio PRIVATE PkgConfig::FAAD2)
endif()

if(TARGET PkgConfig::FAAC)
    target_link_libraries(traversoaudiofileio PRIVATE PkgConfig::FAAC)
endif()
