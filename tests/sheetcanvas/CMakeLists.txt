# SPDX-FileCopyrightText: 2026 Remon Sijrier
# SPDX-License-Identifier: GPL-2.0-or-later

# Headless test of the frame clock's rate filter
add_executable(traverso_frame_rate_filter_test test_frame_rate_filter.cpp)

target_include_directories(traverso_frame_rate_filter_test PRIVATE ${CMAKE_SOURCE_DIR}/src/sheetcanvas)

target_link_libraries(traverso_frame_rate_filter_test PRIVATE traverso_compiler_settings Qt6::Core)

add_test(NAME frame_rate_filter COMMAND traverso_frame_rate_filter_test)
