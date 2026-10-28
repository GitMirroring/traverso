# SPDX-FileCopyrightText: 2026 Remon Sijrier
# SPDX-License-Identifier: GPL-2.0-or-later

# Headless test of the audio thread watchdog
add_executable(traverso_audio_thread_watchdog_test test_audio_thread_watchdog.cpp)

target_include_directories(traverso_audio_thread_watchdog_test PRIVATE ${CMAKE_SOURCE_DIR}/src/audioruntime)

target_link_libraries(traverso_audio_thread_watchdog_test PRIVATE traverso_compiler_settings Qt6::Core)

add_test(NAME audio_thread_watchdog COMMAND traverso_audio_thread_watchdog_test)

# Headless test of the published transport position and the heard position estimate
add_executable(traverso_transport_position_test test_transport_position.cpp)
target_include_directories(traverso_transport_position_test PRIVATE ${CMAKE_SOURCE_DIR}/src/audioruntime)
target_link_libraries(traverso_transport_position_test PRIVATE traverso_compiler_settings traversosupport Qt6::Core)
add_test(NAME transport_position COMMAND traverso_transport_position_test)

# Headless test of the audio thread's closure type
add_executable(traverso_realtime_closure_test test_realtime_closure.cpp)
target_include_directories(traverso_realtime_closure_test PRIVATE ${CMAKE_SOURCE_DIR}/src/audioruntime)
target_link_libraries(traverso_realtime_closure_test PRIVATE traverso_compiler_settings traversosupport Qt6::Core)
add_test(NAME realtime_closure COMMAND traverso_realtime_closure_test)

# Headless test of the denormal protection on the audio thread (T51): a
# Dummy-driver cycle in a process without Traverso
add_executable(traverso_denormals_test test_denormals.cpp)
target_link_libraries(traverso_denormals_test PRIVATE traverso_compiler_settings traversoaudioruntime traversodsp traversosupport Qt6::Core)
add_test(NAME denormals COMMAND traverso_denormals_test)

# Headless test of TAudioDevice with the Dummy driver: start, clients,
# free-wheeling, CPU time, removal, driver switch, shutdown (T58 step 1)
add_executable(traverso_audio_device_test test_audio_device.cpp)
target_link_libraries(traverso_audio_device_test PRIVATE traverso_compiler_settings traversoaudioruntime traversodsp traversosupport Qt6::Core)
add_test(NAME audio_device COMMAND traverso_audio_device_test)
