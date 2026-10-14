# SPDX-FileCopyrightText: 2026 Remon Sijrier
# SPDX-License-Identifier: GPL-2.0-or-later

# Headless test of curve evaluation, without the rest of Traverso
add_executable(traverso_curve_test
    test_curve_snapshot.cpp
    ${CMAKE_SOURCE_DIR}/src/core/TCurveSnapshot.cpp
)

target_include_directories(traverso_curve_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/common
)

target_link_libraries(traverso_curve_test PRIVATE traverso_compiler_settings Qt6::Core)

add_test(NAME curve_snapshot COMMAND traverso_curve_test)
