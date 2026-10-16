# SPDX-FileCopyrightText: 2026 Remon Sijrier
# SPDX-License-Identifier: GPL-2.0-or-later

# Headless test of the TraversoDAW::Float comparisons
add_executable(traverso_float_test test_float.cpp)

target_link_libraries(traverso_float_test PRIVATE traverso_compiler_settings traversosupport Qt6::Core)

add_test(NAME float_compare COMMAND traverso_float_test)
