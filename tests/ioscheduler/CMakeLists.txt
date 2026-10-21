# SPDX-FileCopyrightText: 2026 Remon Sijrier
# SPDX-License-Identifier: GPL-2.0-or-later

# Headless test of the scheduler's round planner: order, hysteresis, seeks,
# the idle limit, rounds without waiting for the tick. Links the library
# only, no core.
add_executable(traverso_io_planner_test test_io_planner.cpp)
target_link_libraries(traverso_io_planner_test PRIVATE traverso_compiler_settings traversoioscheduler)
add_test(NAME io_planner COMMAND traverso_io_planner_test)
