# SPDX-FileCopyrightText: 2026 Remon Sijrier
# SPDX-License-Identifier: GPL-2.0-or-later

# Headless test of TFileIOBuffer: a buffer that served a stereo source also
# holds a source with more channels
add_executable(traverso_file_io_buffer_test test_file_io_buffer.cpp)
target_link_libraries(traverso_file_io_buffer_test PRIVATE traverso_compiler_settings traversoaudiofileio traversodsp traversosupport Qt6::Core)
add_test(NAME file_io_buffer COMMAND traverso_file_io_buffer_test)
