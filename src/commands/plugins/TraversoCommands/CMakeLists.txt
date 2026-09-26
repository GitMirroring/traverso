# Define the source files for the command plugin implementation
set(TRAVERSO_COMMAND_PLUGIN_SOURCES
    TraversoCommands.cpp
)

# Define the target name for the command plugin library
set(TRAVERSO_COMMAND_PLUGIN_LIBRARY "tcp_traversocommands")

# Define the static library for the Qt command plugin
add_library(${TRAVERSO_COMMAND_PLUGIN_LIBRARY} STATIC ${TRAVERSO_COMMAND_PLUGIN_SOURCES})

# Inherit global compiler configurations, flags, and C++17 standards
target_link_libraries(tcp_traversocommands PRIVATE traverso_compiler_settings)

# Configure include directory tracking for compiling components and downstream consumers
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

# Apply mandatory preprocessor definitions required for static Qt plugins
target_compile_definitions(tcp_traversocommands
    PRIVATE
        QT_PLUGIN
        QT_STATICPLUGIN
)

# Link internal project libraries to resolve command classes, base plugin interfaces,
# and core Qt Meta-Object structures required by the plugin implementation.
target_link_libraries(tcp_traversocommands
    PUBLIC
        traversocommands
        traversocore
        Qt6::Widgets
        Qt6::Xml
        Qt6::Core
)
