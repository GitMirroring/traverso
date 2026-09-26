# Define the static library for managing audio file input and output processes
add_library(traversoaudiofileio STATIC
    decode/AbstractAudioReader.cpp
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
target_include_directories(traversoaudiofileio
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/decode
        ${CMAKE_CURRENT_SOURCE_DIR}/encode
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/common
)

# Connect foundational dependencies using uniform PkgConfig target identifiers
target_link_libraries(traversoaudiofileio
    PUBLIC
        Qt6::Core
        Qt6::Xml
    PRIVATE
        PkgConfig::SNDFILE
        PkgConfig::WAVPACK
        PkgConfig::SAMPLERATE
        PkgConfig::MAD
)

# Connect conditional dependencies if their targets were instantiated by the root
if(TARGET PkgConfig::FAAD2)
    target_link_libraries(traversoaudiofileio PUBLIC PkgConfig::FAAD2)
endif()

if(TARGET PkgConfig::FAAC)
    target_link_libraries(traversoaudiofileio PUBLIC PkgConfig::FAAC)
endif()

if(TARGET PkgConfig::SOXR)
    target_link_libraries(traversoaudiofileio PUBLIC PkgConfig::SOXR)
    target_compile_definitions(traversoaudiofileio PUBLIC HAVE_SOXR)
endif()
