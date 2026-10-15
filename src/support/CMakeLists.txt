# SPDX-FileCopyrightText: 2026 Remon Sijrier
# SPDX-License-Identifier: GPL-2.0-or-later

# Shared building blocks without domain logic or runtime state, used by all
# Traverso libraries: time, ring buffers, debugging and small utilities.
# Depends on Qt Core only.

add_library(traversosupport STATIC
    Debugger.h Debugger.cpp
    Utils.h Utils.cpp
    TTimeRef.h TTimeRef.cpp
    defines.h
    RingBufferNPT.h
    TRealTimeLinkedList.h
    TTransportControl.h TTransportControl.cpp
    TProcessCallBackData.h
    TAudioThreadMessageQueue.h TAudioThreadMessageQueue.cpp
)

target_include_directories(traversosupport PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(traversosupport PRIVATE traverso_compiler_settings Qt6::Core)
