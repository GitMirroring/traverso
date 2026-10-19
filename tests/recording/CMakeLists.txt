# SPDX-FileCopyrightText: 2026 Remon Sijrier
# SPDX-License-Identifier: GPL-2.0-or-later

# Headless test of the recording latency compensation
add_executable(traverso_recording_latency_test test_recording_latency.cpp)
target_include_directories(traverso_recording_latency_test PRIVATE ${CMAKE_SOURCE_DIR}/src/core)
target_link_libraries(traverso_recording_latency_test PRIVATE traverso_compiler_settings traversosupport Qt6::Core)
add_test(NAME recording_latency COMMAND traverso_recording_latency_test)
