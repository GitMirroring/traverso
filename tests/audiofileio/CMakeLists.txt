# SPDX-FileCopyrightText: 2026 Remon Sijrier
# SPDX-License-Identifier: GPL-2.0-or-later

# Headless test of TFileIOBuffer: a buffer that served a stereo source also
# holds a source with more channels
add_executable(traverso_file_io_buffer_test test_file_io_buffer.cpp)
target_link_libraries(traverso_file_io_buffer_test PRIVATE traverso_compiler_settings traversoaudiofileio traversodsp traversosupport Qt6::Core)
add_test(NAME file_io_buffer COMMAND traverso_file_io_buffer_test)

# Headless test of SFAudioWriter's header: a file still being written (after
# a kill or a crash) reads in full
add_executable(traverso_sf_writer_header_test test_sf_writer_header.cpp)
target_link_libraries(traverso_sf_writer_header_test PRIVATE traverso_compiler_settings traversoaudiofileio traversodsp traversosupport Qt6::Core)
add_test(NAME sf_writer_header COMMAND traverso_sf_writer_header_test)

# Headless test of WavPack with any channel count: the channel mask written,
# every channel read back
add_executable(traverso_wavpack_channels_test test_wavpack_channels.cpp)
target_link_libraries(traverso_wavpack_channels_test PRIVATE traverso_compiler_settings traversoaudiofileio traversodsp traversosupport Qt6::Core PkgConfig::WAVPACK)
add_test(NAME wavpack_channels COMMAND traverso_wavpack_channels_test)
