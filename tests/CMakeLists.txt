# SPDX-FileCopyrightText: 2026 Remon Sijrier
# SPDX-License-Identifier: GPL-2.0-or-later

# What the tests share, headers only: the signals and the seed of their own
# randomness (common/)
add_library(traverso_test_common INTERFACE)
target_include_directories(traverso_test_common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/common)

add_subdirectory(support)
add_subdirectory(dsp)
add_subdirectory(audiofileio)
add_subdirectory(curves)
add_subdirectory(audioruntime)
add_subdirectory(peaks)
add_subdirectory(ioscheduler)
add_subdirectory(streams)
add_subdirectory(resampling)
add_subdirectory(recording)
