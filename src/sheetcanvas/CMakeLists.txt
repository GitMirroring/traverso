INCLUDE_DIRECTORIES(
${CMAKE_SOURCE_DIR}/src/commands
${CMAKE_SOURCE_DIR}/src/common
${CMAKE_SOURCE_DIR}/src/core
${CMAKE_SOURCE_DIR}/src/engine
${CMAKE_SOURCE_DIR}/src/plugins
${CMAKE_SOURCE_DIR}/src/plugins/native
${CMAKE_SOURCE_DIR}/src/plugins/LV2
${CMAKE_SOURCE_DIR}/src/traverso
${CMAKE_SOURCE_DIR}/src/traverso/dialogs
)

SET(TRAVERSO_SONGCANVAS_SOURCES
TAudioClipView.cpp TAudioClipView.h
TAudioTrackView.cpp TAudioTrackView.h
TClipsViewPort.cpp TClipsViewPort.h
Cursors.cpp Cursors.h
TCurveNodeView.cpp TCurveNodeView.h
TCurveView.cpp TCurveView.h
TFadeCurveView.cpp TFadeCurveView.h
TTimeLineMarkerView.cpp TTimeLineMarkerView.h
TLineView.cpp TLineView.h
TAudioPluginChainView.cpp TAudioPluginChainView.h
TAudioPluginView.cpp TAudioPluginView.h
TPositionIndicator.cpp TPositionIndicator.h
TTrackView.cpp TTrackView.h
TTrackLaneView.cpp TTrackLaneView.h
TSheetView.cpp TSheetView.h
TSheetWidget.cpp TSheetWidget.h
TBusTrackView.cpp TBusTrackView.h
TTimeLineRulerView.cpp TTimeLineRulerView.h
TTimeLineRulerViewPort.cpp TTimeLineRulerViewPort.h
TTrackPanelView.cpp TTrackPanelView.h
TTrackPanelViewPort.cpp TTrackPanelViewPort.h
TViewItem.cpp TViewItem.h
TViewPort.cpp TViewPort.h
dialogs/AudioClipEditDialog.cpp dialogs/AudioClipEditDialog.h
dialogs/TTrackManagerDialog.cpp dialogs/TTrackManagerDialog.h
TVUMeterView.cpp TVUMeterView.h
TCanvasCursor.cpp TCanvasCursor.h
TKnobView.cpp TKnobView.h
TTextView.cpp TTextView.h
)


SET(TRAVERSO_SONGCANVAS_LIBRARY "traversosheetcanvas")

ADD_LIBRARY(${TRAVERSO_SONGCANVAS_LIBRARY} STATIC 
	${TRAVERSO_SONGCANVAS_SOURCES} 
)

IF(USE_PCH)
    ADD_DEPENDENCIES(traversosheetcanvas precompiled_headers)
ENDIF(USE_PCH)
