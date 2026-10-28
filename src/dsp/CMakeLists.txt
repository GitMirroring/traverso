# SPDX-FileCopyrightText: 2026 Remon Sijrier
# SPDX-License-Identifier: GPL-2.0-or-later

# DSP building blocks: audio buffers with their gain and mix functions (with
# SSE versions), CPU feature detection, dithering and sample rate conversion.
# Used by the audio runtime, audio file io and core.

enable_language(ASM)

set(DSP_ASM_SOURCE "")
if(WANT_SSE)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(DSP_ASM_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/sse_functions_64bit.S)
    else()
        set(DSP_ASM_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/sse_functions.S)
    endif()
    set_source_files_properties(${DSP_ASM_SOURCE} PROPERTIES LANGUAGE ASM)
endif()

add_library(traversodsp STATIC
    TChannelBuffer.h TChannelBuffer.cpp
    Mixer.h Mixer.cpp
    fpu.h fpu.cc
    gdither.h gdither_types.h gdither.cpp
    noise.h
    TAudioResampler.h TAudioResampler.cpp
    ${DSP_ASM_SOURCE}
)

target_include_directories(traversodsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(traversodsp
    PRIVATE
        traverso_compiler_settings
        traversosupport
        Qt6::Core
        PkgConfig::SAMPLERATE
        PkgConfig::SOXR
)
