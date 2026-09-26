SET(TRAVERSO_PLUGINS_SOURCES
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

SET(TRAVERSO_PLUGINS_LIBRARY "traversoplugins")
ADD_LIBRARY(${TRAVERSO_PLUGINS_LIBRARY} STATIC ${TRAVERSO_PLUGINS_SOURCES})

target_include_directories(traversoplugins
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/native
        ${CMAKE_CURRENT_SOURCE_DIR}/LV2
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/common
        ${CMAKE_SOURCE_DIR}/src/engine
        ${CMAKE_SOURCE_DIR}/src/commands
)

target_link_libraries(traversoplugins
    PUBLIC
        Qt6::Widgets
        Qt6::Xml
        PkgConfig::FFTW3
)

IF(LIBLILV_FOUND)
    target_sources(traversoplugins PRIVATE LV2/LV2Plugin.cpp)
    target_link_libraries(traversoplugins PUBLIC PkgConfig::LIBLILV)
ENDIF()

IF(USE_INTERNAL_SLV2_LIB)
    target_include_directories(traversoplugins PRIVATE ${CMAKE_SOURCE_DIR}/src/3rdparty/slv2)
ENDIF()
