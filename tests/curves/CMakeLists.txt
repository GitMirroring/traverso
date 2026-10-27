# SPDX-FileCopyrightText: 2026 Remon Sijrier
# SPDX-License-Identifier: GPL-2.0-or-later

# Headless test of curve evaluation, without the rest of Traverso
add_executable(traverso_curve_test
    test_curve_snapshot.cpp
    ${CMAKE_SOURCE_DIR}/src/core/TCurveSnapshot.cpp
)

target_include_directories(traverso_curve_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/support
)

target_link_libraries(traverso_curve_test PRIVATE traverso_compiler_settings traverso_test_common Qt6::Core)

add_test(NAME curve_snapshot COMMAND traverso_curve_test)

# Headless test of the curve polyline the curve views draw
add_executable(traverso_curve_polyline_test
    test_curve_polyline.cpp
    ${CMAKE_SOURCE_DIR}/src/core/TCurveSnapshot.cpp
)

target_include_directories(traverso_curve_polyline_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/support
    ${CMAKE_SOURCE_DIR}/src/sheetcanvas
)

target_link_libraries(traverso_curve_polyline_test PRIVATE traverso_compiler_settings traverso_test_common Qt6::Core Qt6::Gui)

add_test(NAME curve_polyline COMMAND traverso_curve_polyline_test)

# Headless test of the fade shape
add_executable(traverso_fade_shape_test
    test_fade_shape.cpp
    ${CMAKE_SOURCE_DIR}/src/core/TFadeShape.cpp
    ${CMAKE_SOURCE_DIR}/src/core/TCurveSnapshot.cpp
)

target_include_directories(traverso_fade_shape_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/support
)

target_link_libraries(traverso_fade_shape_test PRIVATE traverso_compiler_settings Qt6::Core)

add_test(NAME fade_shape COMMAND traverso_fade_shape_test)
