# SPDX-FileCopyrightText: 2026 Remon Sijrier
# SPDX-License-Identifier: GPL-2.0-or-later

# Headless test of the DiskIO round planner: order, hysteresis, seeks, the
# idle limit, rounds without waiting for the tick; and when a stream needs
# disk work
add_executable(traverso_diskio_planner_test test_diskio_planner.cpp ${CMAKE_SOURCE_DIR}/src/core/TDiskIOPlanner.cpp)
target_include_directories(traverso_diskio_planner_test PRIVATE ${CMAKE_SOURCE_DIR}/src/core)
target_link_libraries(traverso_diskio_planner_test PRIVATE traverso_compiler_settings Qt6::Core)
add_test(NAME diskio_planner COMMAND traverso_diskio_planner_test)
