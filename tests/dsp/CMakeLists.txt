# SPDX-FileCopyrightText: 2026 Remon Sijrier
# SPDX-License-Identifier: GPL-2.0-or-later

# Headless test of the fader law (TFaderLaw)
add_executable(traverso_fader_law_test test_fader_law.cpp)
target_link_libraries(traverso_fader_law_test PRIVATE traverso_compiler_settings traversodsp traversosupport Qt6::Core)
add_test(NAME fader_law COMMAND traverso_fader_law_test)

# Headless test of TAudioBuffer (the channels of a read or a write): a
# buffer that served a stereo source has room for a source with more channels
add_executable(traverso_audio_buffer_test test_audio_buffer.cpp)
target_link_libraries(traverso_audio_buffer_test PRIVATE traverso_compiler_settings traversodsp traversosupport Qt6::Core)
add_test(NAME audio_buffer COMMAND traverso_audio_buffer_test)
