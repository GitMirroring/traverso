# SPDX-FileCopyrightText: 2026 Remon Sijrier
# SPDX-License-Identifier: GPL-2.0-or-later

set(TRAVERSO_PEAK_SOURCES
    ${CMAKE_SOURCE_DIR}/src/core/TPeakPyramid.cpp
    ${CMAKE_SOURCE_DIR}/src/core/TPeakFile.cpp
    ${CMAKE_SOURCE_DIR}/src/core/TPeakCache.cpp
    ${CMAKE_SOURCE_DIR}/src/core/TPeakStream.cpp
    ${CMAKE_SOURCE_DIR}/src/core/TWaveformColumns.cpp
)

# Headless test of the peak pyramid, the peak file, its stream and the
# waveform columns, against brute force over the samples
add_executable(traverso_peak_test test_peaks.cpp ${TRAVERSO_PEAK_SOURCES})
target_include_directories(traverso_peak_test PRIVATE ${CMAKE_SOURCE_DIR}/src/core)
target_link_libraries(traverso_peak_test PRIVATE traverso_compiler_settings traversosupport Qt6::Core)
add_test(NAME peaks COMMAND traverso_peak_test)

# Headless test of the peak build in steps: the same file as appending at
# once, sources that end early, fail or are cancelled
add_executable(traverso_peak_build_test test_peak_build.cpp ${TRAVERSO_PEAK_SOURCES} ${CMAKE_SOURCE_DIR}/src/core/TPeakBuildTask.cpp)
target_include_directories(traverso_peak_build_test PRIVATE ${CMAKE_SOURCE_DIR}/src/core)
target_link_libraries(traverso_peak_build_test PRIVATE traverso_compiler_settings traversoaudiofileio traversodsp traversosupport Qt6::Core)
add_test(NAME peak_build COMMAND traverso_peak_build_test)

# Headless test of the peak cache: the key, marking use, the files in use,
# the eviction; the format version, the source time tolerance, a recording
# written where its waveform looks
add_executable(traverso_peak_cache_test test_peak_cache.cpp ${TRAVERSO_PEAK_SOURCES})
target_include_directories(traverso_peak_cache_test PRIVATE ${CMAKE_SOURCE_DIR}/src/core)
target_link_libraries(traverso_peak_cache_test PRIVATE traverso_compiler_settings traversosupport Qt6::Core)
add_test(NAME peak_cache COMMAND traverso_peak_cache_test)

# Benchmark of the column query and the envelope painting, not part of ctest.
# Build it in Release.
add_executable(traverso_peak_benchmark benchmark_peaks.cpp ${TRAVERSO_PEAK_SOURCES})
target_include_directories(traverso_peak_benchmark PRIVATE ${CMAKE_SOURCE_DIR}/src/core)
target_link_libraries(traverso_peak_benchmark PRIVATE traverso_compiler_settings traversosupport Qt6::Core Qt6::Gui)

# Measurements on real audio files (doc/design/peaks-v6-report.md), not part
# of ctest. Build it in Release.
add_executable(traverso_peak_measure EXCLUDE_FROM_ALL measure_peaks.cpp ${TRAVERSO_PEAK_SOURCES} ${CMAKE_SOURCE_DIR}/src/core/TPeakBuildTask.cpp)
target_include_directories(traverso_peak_measure PRIVATE ${CMAKE_SOURCE_DIR}/src/core)
target_link_libraries(traverso_peak_measure PRIVATE traverso_compiler_settings traversoaudiofileio traversodsp traversosupport Qt6::Core)
