# Track visual design resources mapping spatial representation tracking tools
set(TRAVERSO_SHEETCANVAS_SOURCES
    TAudioClipView.cpp
    TAudioTrackView.cpp
    TClipsViewPort.cpp
    TWorkCursorView.cpp
    TPlayHeadView.cpp
    TCurveNodeView.cpp
    TCurveView.cpp
    TFadeCurveView.cpp
    TTimeLineMarkerView.cpp
    TLineView.cpp
    TAudioPluginChainView.cpp
    TAudioPluginView.cpp
    TPositionIndicator.cpp
    TTrackView.cpp
    TTrackLaneView.cpp
    TSheetView.cpp
    TSheetWidget.cpp
    TBusTrackView.cpp
    TTimeLineRulerView.cpp
    TTimeLineRulerViewPort.cpp
    TTrackPanelView.cpp
    TTrackPanelViewPort.cpp
    TViewItem.cpp
    TViewPort.cpp
    dialogs/AudioClipEditDialog.cpp
    dialogs/TTrackManagerDialog.cpp
    TVUMeterView.cpp
    TCanvasCursor.cpp
    TKnobView.cpp
    TTextView.cpp
    ClipTileCache.cpp
)

# Define the dynamic timeline canvas representation pipeline structure
add_library(traversosheetcanvas STATIC ${TRAVERSO_SHEETCANVAS_SOURCES})
target_link_libraries(traversosheetcanvas PRIVATE traverso_compiler_settings)

# Declare comprehensive inclusion references traversing the workspace architecture
target_include_directories(traversosheetcanvas
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/dialogs
        ${CMAKE_SOURCE_DIR}/src/traverso
        ${CMAKE_SOURCE_DIR}/src/traverso/dialogs
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/common
        ${CMAKE_SOURCE_DIR}/src/plugins
        ${CMAKE_SOURCE_DIR}/src/plugins/native
        ${CMAKE_SOURCE_DIR}/src/commands
        ${CMAKE_SOURCE_DIR}/src/audiofileio
        ${CMAKE_SOURCE_DIR}/src/engine
)

# Connect supporting interface drawing mechanics to the canvas engine
target_link_libraries(traversosheetcanvas
    PUBLIC
        Qt6::Widgets
        Qt6::Gui
        Qt6::Xml
        Qt6::Core
        Qt6::Concurrent
)
