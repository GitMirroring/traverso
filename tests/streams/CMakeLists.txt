# SPDX-FileCopyrightText: 2026 Remon Sijrier
# SPDX-License-Identifier: GPL-2.0-or-later

# Headless test of the stream status (core, header only): when a stream needs
# work from the scheduler
add_executable(traverso_stream_status_test test_stream_status.cpp)
target_include_directories(traverso_stream_status_test PRIVATE ${CMAKE_SOURCE_DIR}/src/core)
target_link_libraries(traverso_stream_status_test PRIVATE traverso_compiler_settings Qt6::Core)
add_test(NAME stream_status COMMAND traverso_stream_status_test)
