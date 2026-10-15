# SPDX-FileCopyrightText: 2026 Remon Sijrier
# SPDX-License-Identifier: GPL-2.0-or-later

# Headless test of the audio thread watchdog
add_executable(traverso_audio_thread_watchdog_test test_audio_thread_watchdog.cpp)

target_include_directories(traverso_audio_thread_watchdog_test PRIVATE ${CMAKE_SOURCE_DIR}/src/engine)

target_link_libraries(traverso_audio_thread_watchdog_test PRIVATE traverso_compiler_settings Qt6::Core)

add_test(NAME audio_thread_watchdog COMMAND traverso_audio_thread_watchdog_test)
