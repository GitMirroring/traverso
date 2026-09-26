SET(TRAVERSO_SHEETCANVAS_SOURCES
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

SET(TRAVERSO_SHEETCANVAS_LIBRARY "traversosheetcanvas")
ADD_LIBRARY(${TRAVERSO_SHEETCANVAS_LIBRARY} STATIC ${TRAVERSO_SHEETCANVAS_SOURCES})

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

target_link_libraries(traversosheetcanvas
    PUBLIC
        Qt6::Xml
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
)

IF(USE_PCH)
    target_precompile_headers(traversosheetcanvas REUSE_FROM traversocore)
ENDIF()
