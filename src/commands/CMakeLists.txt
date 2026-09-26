SET(TRAVERSO_COMMANDS_SOURCES
    ArrowKeyBrowser.cpp
    AudioClipExternalProcessing.cpp
    TAddRemoveCommand.cpp
    ClipSelection.cpp
    CommandGroup.cpp
    Crop.cpp
    ExternalProcessingDialog.cpp
    Fade.cpp
    GainCommand.cpp
    TGainGroupCommand.cpp
    TAudioFileImportCommand.cpp
    TFadeRangeCommand.cpp
    MoveClip.cpp
    MoveCurveNode.cpp
    MoveEdge.cpp
    MoveMarker.cpp
    MovePlugin.cpp
    MoveTrack.cpp
    PCommand.cpp
    TSplitAudioClipCommand.cpp
    TAudioClipDualTrimCommand.cpp
    TrackPan.cpp
    Zoom.cpp
    ArmTracks.cpp
    PlayHeadMove.cpp
    WorkCursorMove.cpp
    RemoveClip.cpp
    TMoveCommand.cpp
)

ADD_LIBRARY(traversocommands STATIC ${TRAVERSO_COMMANDS_SOURCES})

target_include_directories(traversocommands
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src/traverso
        ${CMAKE_SOURCE_DIR}/src/traverso/dialogs
        ${CMAKE_SOURCE_DIR}/src/sheetcanvas
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/common
        ${CMAKE_SOURCE_DIR}/src/audiofileio
)

target_link_libraries(traversocommands
    PUBLIC
        Qt6::Core
        Qt6::Xml
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
)

ADD_SUBDIRECTORY(plugins)

IF(USE_PCH)
    target_precompile_headers(traversocommands REUSE_FROM traversocore)
ENDIF()
