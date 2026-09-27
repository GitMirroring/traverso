set(TRAVERSO_PLUGINS_SOURCES
    TAudioPluginChain.cpp
    TAudioPlugin.cpp
    PluginManager.cpp
    TAudioPluginSliderWidget.cpp
    native/CorrelationMeter.cpp
    native/SpectralMeter.cpp
    native/GainEnvelope.cpp
    TAudioPluginPropertiesDialog.cpp
    TAudioPluginPort.cpp
    TAudioPluginControlPort.cpp
    TAudioPluginInputPort.cpp
    TAudioPluginOutputPort.cpp
)
# Define the management framework for application processing effects
add_library(traversoplugins STATIC ${TRAVERSO_PLUGINS_SOURCES})
target_link_libraries(traversoplugins PRIVATE traverso_compiler_settings)

# Configure scoped parameter visibility profiles across system subdirectories
# Public scopes allow components like dialogs/ to locate internal plugin headers
target_include_directories(traversoplugins
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/native
        ${CMAKE_CURRENT_SOURCE_DIR}/LV2
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/common
        ${CMAKE_SOURCE_DIR}/src/engine
        ${CMAKE_SOURCE_DIR}/src/commands
)

# Map foundational transformation structures and metadata toolsets
target_link_libraries(traversoplugins
    PUBLIC
        Qt6::Core
        Qt6::Widgets
        Qt6::Xml
        PkgConfig::FFTW3
)

# Inject standard multi-channel system extensions if package targets resolve positively
if(TARGET PkgConfig::LIBLILV)
    target_sources(traversoplugins PRIVATE LV2/LV2Plugin.cpp)
    target_link_libraries(traversoplugins PUBLIC PkgConfig::LIBLILV)
endif()
