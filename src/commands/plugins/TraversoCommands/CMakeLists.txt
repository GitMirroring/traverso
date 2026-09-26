SET(TRAVERSO_COMMAND_PLUGIN_SOURCES
    TraversoCommands.cpp
)

SET(TRAVERSO_COMMAND_PLUGIN_LIBRARY "tcp_traversocommands")
ADD_LIBRARY(${TRAVERSO_COMMAND_PLUGIN_LIBRARY} STATIC ${TRAVERSO_COMMAND_PLUGIN_SOURCES})

target_include_directories(tcp_traversocommands
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src/traverso
        ${CMAKE_SOURCE_DIR}/src/common
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/sheetcanvas
        ${CMAKE_SOURCE_DIR}/src/commands
        ${CMAKE_SOURCE_DIR}/src/plugins
        ${CMAKE_SOURCE_DIR}/src/plugins/native
)

target_compile_definitions(tcp_traversocommands
    PRIVATE
        QT_PLUGIN
        QT_STATICPLUGIN
)

target_link_libraries(tcp_traversocommands
    PUBLIC
        Qt6::Widgets
        Qt6::Xml
        Qt6::Core
)
