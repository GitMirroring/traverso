ADD_LIBRARY(traversoaudiofileio STATIC
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

IF(HAVE_MP3_ENCODING)
    target_sources(traversoaudiofileio PRIVATE encode/LameAudioWriter.cpp)
ENDIF()

IF(HAVE_M4A_ENCODING)
    target_sources(traversoaudiofileio PRIVATE encode/FaacAudioWriter.cpp)
ENDIF()

IF(HAVE_M4A_DECODING)
    target_sources(traversoaudiofileio PRIVATE decode/FaadAudioReader.cpp decode/minimp4.h)
ENDIF()

target_include_directories(traversoaudiofileio
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/decode
        ${CMAKE_CURRENT_SOURCE_DIR}/encode
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/common
)

target_link_libraries(traversoaudiofileio
    PUBLIC
        Qt6::Core
        Qt6::Xml
        PkgConfig::SNDFILE
        PkgConfig::WAVPACK
        samplerate
    PRIVATE
        mad
)

if(HAVE_M4A_DECODING)
    target_link_libraries(traversoaudiofileio PUBLIC PkgConfig::FAAD2)
endif()

if(HAVE_M4A_ENCODING)
    target_link_libraries(traversoaudiofileio PUBLIC PkgConfig::FAAC)
endif()

if(SOXR_FOUND)
    target_link_libraries(traversoaudiofileio PUBLIC PkgConfig::SOXR)
    target_compile_definitions(traversoaudiofileio PUBLIC HAVE_SOXR)
endif()

IF(USE_PCH)
    target_precompile_headers(traversoaudiofileio REUSE_FROM traversocore)
ENDIF()
