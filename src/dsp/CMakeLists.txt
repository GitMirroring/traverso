# SPDX-FileCopyrightText: 2026 Remon Sijrier
# SPDX-License-Identifier: GPL-2.0-or-later

# DSP building blocks: audio buffers with their gain and mix functions (with
# an AVX2 build of them), CPU feature detection, dithering and sample rate
# conversion. Used by the audio runtime, audio file io and core.

add_library(traversodsp STATIC
    TChannelBuffer.h TChannelBuffer.cpp
    TAudioBuffer.h
    TBufferOperations.h TBufferOperations.cpp TBufferOperationsLoops.h
    TFaderLaw.h TFaderLaw.cpp
    TGain.h
    TCpuFeatures.h TCpuFeatures.cpp
    TDenormals.h TDenormals.cpp
    gdither.h gdither_types.h gdither.cpp
    noise.h
    TAudioResampler.h TAudioResampler.cpp
)

# The buffer operations built for AVX2 as well, on x86-64 with GCC or Clang
# (MinGW too); TBufferOperations uses them when the CPU has AVX2 (T20). Not
# on macOS, which uses Accelerate. The compiler's target decides, not
# CMAKE_SYSTEM_PROCESSOR (the build machine without a toolchain file: wrong
# for -m32 and cross builds); the build machine's CPU does not matter, the
# CPU check is at run time (TCpuFeatures)
include(CheckCXXSourceCompiles)
check_cxx_source_compiles("
#if !defined(__x86_64__)
#error not x86-64
#endif
int main() { return 0; }" TRAVERSO_COMPILER_TARGETS_X86_64)

if(TRAVERSO_COMPILER_TARGETS_X86_64 AND NOT APPLE
   AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_sources(traversodsp PRIVATE TBufferOperationsAVX2.cpp)
    set_source_files_properties(TBufferOperationsAVX2.cpp PROPERTIES COMPILE_OPTIONS -mavx2)
    target_compile_definitions(traversodsp PRIVATE TRAVERSO_AVX2_OPERATIONS)
endif()

target_include_directories(traversodsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(traversodsp
    PRIVATE
        traverso_compiler_settings
        traversosupport
        Qt6::Core
        PkgConfig::SAMPLERATE
        PkgConfig::SOXR
)
