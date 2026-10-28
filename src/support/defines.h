#pragma once

#include "TTypes.h"

// The audio file format enums wait here until T15 moves them to audiofileio.
namespace TraversoDAW {

enum class WriterType {
    SNDFILE,
    WAVPACK,
    M4A
};

enum class FileFormat {
    UNKNOWN,
    WAV,
    AIFF,
    W64,
    FLAC,
    OGG,
    MP3,
    WAVPACK,
    RAW
};

enum class DataFormat {
    FLOAT,
    PCM_S8,
    PCM_16,
    PCM_24,
    PCM_32
};

enum class BitrateMode {
    CONSTANT,
    AVERAGE,
    VARIABLE,
    UNKNOWN
};

}
