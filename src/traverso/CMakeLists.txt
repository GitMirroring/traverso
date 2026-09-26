SET(TRAVERSO_GUI_SOURCES
    ${CMAKE_SOURCE_DIR}/src/common/fpu.cc
    Main.cpp
    TMainWindow.cpp
    Traverso.cpp
    dialogs/settings/Pages.cpp
    dialogs/settings/SettingsDialog.cpp
    dialogs/project/ProjectManagerDialog.cpp
    dialogs/InsertSilenceDialog.cpp
    dialogs/MarkerDialog.cpp
    dialogs/project/NewSheetDialog.cpp
    dialogs/project/NewProjectDialog.cpp
    dialogs/project/OpenProjectDialog.cpp
    dialogs/project/NewTrackDialog.cpp
    dialogs/project/ImportClipsDialog.cpp
    dialogs/RestoreProjectBackupDialog.cpp
    dialogs/PluginSelectorDialog.cpp
    dialogs/ProjectConverterDialog.cpp
    dialogs/ExportDialog.cpp
    dialogs/CDWritingDialog.cpp
    dialogs/ThemeModifierDialog.cpp
    dialogs/TTrackSelector.cpp
    dialogs/TShortcutEditorDialog.cpp
    widgets/TAudioBusVUMonitorWidget.cpp
    widgets/CorrelationMeterWidget.cpp
    widgets/ExportFormatOptionsWidget.cpp
    widgets/InfoWidgets.cpp
    widgets/MessageWidget.cpp
    widgets/MeterWidget.cpp
    widgets/ResourcesWidget.cpp
    widgets/SpectralMeterWidget.cpp
    widgets/SpectralMeterConfigWidget.cpp
    widgets/SpectralMeterView.cpp
    widgets/CorrelationMeterView.cpp
    widgets/MeterView.cpp
    widgets/TransportConsoleWidget.cpp
    widgets/TVUMeterWidget.cpp
    widgets/WelcomeWidget.cpp
    widgets/TSessionTabWidget.cpp
    widgets/TContextHelpWidget.cpp
)

QT6_ADD_RESOURCES(TRAVERSO_RESOURCES ../../resources/traverso.qrc)

ADD_EXECUTABLE(traverso
    ${TRAVERSO_GUI_SOURCES}
    ${TRAVERSO_RESOURCES}
)

target_include_directories(traverso PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

IF(USE_PCH)
    target_precompile_headers(traverso REUSE_FROM traversocore)
ENDIF()

TARGET_LINK_LIBRARIES(traverso
    PRIVATE
        traversocore
        traversoaudiobackend
        traversoaudiofileio
        traversoplugins
        tcp_traversocommands
        traversocommands
        traversosheetcanvas
        Qt6::Widgets
        Qt6::Xml
)


IF(WIN32)
    target_link_libraries(traverso PRIVATE mp3lame brotlidec brotlicommon jbig lerc bz2 lzma deflate zstd tiff mpg123 opus winmm)
ELSEIF(APPLE)
    target_link_libraries(traverso PRIVATE "-framework CoreFoundation" "-framework Accelerate" "-framework ApplicationServices" dl)
ELSE()
    target_link_libraries(traverso PRIVATE dl)
ENDIF()

SET(EXE_NAME "traverso${TAG_VERSION}")
INSTALL(TARGETS traverso RUNTIME DESTINATION bin)
