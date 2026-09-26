INCLUDE_DIRECTORIES(
        ${CMAKE_SOURCE_DIR}/src/audiofileio/decode
        ${CMAKE_SOURCE_DIR}/src/audiofileio/encode
        ${CMAKE_SOURCE_DIR}/src/core
)

SET(TRAVERSO_AUDIOFILEIO_SOURCES
        decode/AbstractAudioReader.cpp
        decode/SFAudioReader.cpp
        decode/TResampleAudioReader.cpp
        decode/WPAudioReader.cpp
        decode/MadAudioReader.cpp
        encode/AbstractAudioWriter.cpp
        encode/SFAudioWriter.cpp
        encode/WPAudioWriter.cpp
        encode/TResampleAudioWriter.h
        encode/TResampleAudioWriter.cpp
        # encode/FlacAudioWriter.cpp
        # encode/VorbisAudioWriter.cpp
)

IF(HAVE_MP3_ENCODING)
SET(TRAVERSO_AUDIOFILEIO_SOURCES
        ${TRAVERSO_AUDIOFILEIO_SOURCES}
        encode/LameAudioWriter.cpp
)
ENDIF(HAVE_MP3_ENCODING)

IF(HAVE_M4A_ENCODING)
SET(TRAVERSO_AUDIOFILEIO_SOURCES
        ${TRAVERSO_AUDIOFILEIO_SOURCES}
        encode/FaacAudioWriter.cpp
)
ENDIF(HAVE_M4A_ENCODING)

IF(HAVE_M4A_DECODING)
SET(TRAVERSO_AUDIOFILEIO_SOURCES
        ${TRAVERSO_AUDIOFILEIO_SOURCES}
        decode/FaadAudioReader.cpp
        decode/minimp4.h
)
SET(TR)
ENDIF(HAVE_M4A_DECODING)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SOXR REQUIRED IMPORTED_TARGET soxr)


SET(TRAVERSO_AUDIOFILEIO_LIBRARY "traversoaudiofileio")

ADD_LIBRARY(${TRAVERSO_AUDIOFILEIO_LIBRARY} STATIC ${TRAVERSO_AUDIOFILEIO_SOURCES})

if(HAVE_M4A_DECODING)
        target_link_libraries(${TRAVERSO_AUDIOFILEIO_LIBRARY} PUBLIC PkgConfig::FAAD2)
endif()

if(HAVE_M4A_ENCODING)
        target_link_libraries(${TRAVERSO_AUDIOFILEIO_LIBRARY} PUBLIC PkgConfig::FAAC)
endif()

if(SOXR_FOUND)
    target_link_libraries(${TRAVERSO_AUDIOFILEIO_LIBRARY} PUBLIC PkgConfig::SOXR)
    target_compile_definitions(${TRAVERSO_AUDIOFILEIO_LIBRARY} PUBLIC HAVE_SOXR)
endif()

IF(USE_PCH)
ADD_DEPENDENCIES(traversoaudiofileio precompiled_headers)
ENDIF(USE_PCH)
