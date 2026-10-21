# SPDX-FileCopyrightText: 2026 Remon Sijrier
# SPDX-License-Identifier: GPL-2.0-or-later

# Plans and hands out the IO work of the streams (TIOStream: the readers and
# writers in core) and of background tasks (TIOTask: peak builds), in rounds
# on a thread pool. It does no IO itself and knows no engine: the owner tells
# it the transport, the output rate and the buffer size.
add_library(traversoioscheduler STATIC
    TIOScheduler.h TIOScheduler.cpp
    TIOPlanner.h TIOPlanner.cpp
    TIOTask.h
    TIOStream.h
)

target_link_libraries(traversoioscheduler PRIVATE traverso_compiler_settings)

# Only its own directory: it never includes headers of libraries it does
# not link
target_include_directories(traversoioscheduler PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Its headers include support (TTimeRef, the wake semaphore) and Qt Core
# (QObject): public, so a consumer links the scheduler alone. Qt Concurrent
# is used inside only.
target_link_libraries(traversoioscheduler
    PUBLIC
        traversosupport
        Qt6::Core
    PRIVATE
        Qt6::Concurrent
)
