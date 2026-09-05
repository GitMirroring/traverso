INCLUDE_DIRECTORIES(
${CMAKE_SOURCE_DIR}/src/audiofileio/decode
${CMAKE_SOURCE_DIR}/src/audiofileio/encode
${CMAKE_SOURCE_DIR}/src/core
)

SET(TRAVERSO_AUDIOFILEIO_SOURCES
decode/AbstractAudioReader.cpp
decode/SFAudioReader.cpp
decode/ResampleAudioReader.cpp
decode/WPAudioReader.cpp
encode/AbstractAudioWriter.cpp
encode/SFAudioWriter.cpp
encode/WPAudioWriter.cpp
)

SET(TRAVERSO_AUDIOFILEIO_LIBRARY "traversoaudiofileio")

ADD_LIBRARY(${TRAVERSO_AUDIOFILEIO_LIBRARY} STATIC ${TRAVERSO_AUDIOFILEIO_SOURCES})
