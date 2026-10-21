# SPDX-FileCopyrightText: 2026 Remon Sijrier
# SPDX-License-Identifier: GPL-2.0-or-later

# Headless test of the resampling reader: sweeps read at another rate, the
# way the stream reader reads them, must stay where they belong
add_executable(traverso_resample_reader_test test_resample_reader.cpp)
target_link_libraries(traverso_resample_reader_test PRIVATE traverso_compiler_settings traversoaudiofileio traversodsp traversosupport Qt6::Core)
add_test(NAME resample_reader COMMAND traverso_resample_reader_test)
