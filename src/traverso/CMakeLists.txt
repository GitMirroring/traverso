INCLUDE_DIRECTORIES(
${CMAKE_SOURCE_DIR}/src/audiofileio/decode
${CMAKE_SOURCE_DIR}/src/audiofileio/encode
${CMAKE_SOURCE_DIR}/src/audiofileio/
${CMAKE_SOURCE_DIR}/src/commands
${CMAKE_SOURCE_DIR}/src/common
${CMAKE_SOURCE_DIR}/src/core
${CMAKE_SOURCE_DIR}/src/engine
${CMAKE_SOURCE_DIR}/src/plugins
${CMAKE_SOURCE_DIR}/src/plugins/LV2
${CMAKE_SOURCE_DIR}/src/plugins/native
${CMAKE_SOURCE_DIR}/src/sheetcanvas
)

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

QT6_ADD_RESOURCES(TRAVERSO_RESOURCES
	../../resources/traverso.qrc
)

IF(AUTOPACKAGE_BUILD)
find_program(RELAYTOOL_EXECUTABLE NAMES relaytool)

IF(RELAYTOOL_EXECUTABLE)

execute_process(
	COMMAND relaytool --multilink libjack.so.0 libjack-0.100.0.so.0 --relay jack -ljack
	OUTPUT_VARIABLE relaytool_invoke_result
	RESULT_VARIABLE relaytool_failed
)
string(REGEX REPLACE "\n" "" relaytool_invoke_result "${relaytool_invoke_result}")
string(REGEX REPLACE " +$" "" relaytool_invoke_result "${relaytool_invoke_result}")
SET(JACK_STUB ../../${relaytool_invoke_result})

execute_process(
	COMMAND relaytool --relay FLAC -lFLAC
	OUTPUT_VARIABLE relaytool_invoke_result
	RESULT_VARIABLE relaytool_failed
)
string(REGEX REPLACE "\n" "" relaytool_invoke_result "${relaytool_invoke_result}")
string(REGEX REPLACE " +$" "" relaytool_invoke_result "${relaytool_invoke_result}")
SET(FLAC_STUB ../../${relaytool_invoke_result})

execute_process(
	COMMAND relaytool --relay mad -lmad
	OUTPUT_VARIABLE relaytool_invoke_result
	RESULT_VARIABLE relaytool_failed
)
string(REGEX REPLACE "\n" "" relaytool_invoke_result "${relaytool_invoke_result}")
string(REGEX REPLACE " +$" "" relaytool_invoke_result "${relaytool_invoke_result}")
SET(MAD_STUB ../../${relaytool_invoke_result})

execute_process(
	COMMAND relaytool --relay mp3lame -lmp3lame
	OUTPUT_VARIABLE relaytool_invoke_result
	RESULT_VARIABLE relaytool_failed
)
string(REGEX REPLACE "\n" "" relaytool_invoke_result "${relaytool_invoke_result}")
string(REGEX REPLACE " +$" "" relaytool_invoke_result "${relaytool_invoke_result}")
SET(LAME_STUB ../../${relaytool_invoke_result})

execute_process(
	COMMAND relaytool --relay ogg -logg
	OUTPUT_VARIABLE relaytool_invoke_result
	RESULT_VARIABLE relaytool_failed
)
string(REGEX REPLACE "\n" "" relaytool_invoke_result "${relaytool_invoke_result}")
string(REGEX REPLACE " +$" "" relaytool_invoke_result "${relaytool_invoke_result}")
SET(OGG_STUB ../../${relaytool_invoke_result})

execute_process(
	COMMAND relaytool --relay vorbis -lvorbis
	OUTPUT_VARIABLE relaytool_invoke_result
	RESULT_VARIABLE relaytool_failed
)
string(REGEX REPLACE "\n" "" relaytool_invoke_result "${relaytool_invoke_result}")
string(REGEX REPLACE " +$" "" relaytool_invoke_result "${relaytool_invoke_result}")
SET(VORBIS_STUB ../../${relaytool_invoke_result})

execute_process(
	COMMAND relaytool --relay vorbisfile -lvorbisfile
	OUTPUT_VARIABLE relaytool_invoke_result
	RESULT_VARIABLE relaytool_failed
)
string(REGEX REPLACE "\n" "" relaytool_invoke_result "${relaytool_invoke_result}")
string(REGEX REPLACE " +$" "" relaytool_invoke_result "${relaytool_invoke_result}")
SET(VORBISFILE_STUB ../../${relaytool_invoke_result})

execute_process(
	COMMAND relaytool --relay vorbisenc -lvorbisenc
	OUTPUT_VARIABLE relaytool_invoke_result
	RESULT_VARIABLE relaytool_failed
)
string(REGEX REPLACE "\n" "" relaytool_invoke_result "${relaytool_invoke_result}")
string(REGEX REPLACE " +$" "" relaytool_invoke_result "${relaytool_invoke_result}")
SET(VORBIS_ENC_STUB ../../${relaytool_invoke_result})

MESSAGE(${VORBIS_ENC_STUB})

ENDIF(RELAYTOOL_EXECUTABLE)

ELSE(AUTOPACKAGE_BUILD)
SET(JACK_LIB jack)
SET(FLAC_LIB FLAC)
SET(MAD_LIB mad)
SET(LAME_LIB mp3lame)
SET(OGG_LIB ogg)
SET(VORBIS_LIB vorbis)
SET(VORBIS_FILE_LIB vorbisfile)
SET(VORBIS_ENC_LIB vorbisenc)
ENDIF(AUTOPACKAGE_BUILD)

ADD_EXECUTABLE(traverso
	${TRAVERSO_GUI_SOURCES}
	${TRAVERSO_GUI_UI_SOURCES}
	${TRAVERSO_GUI_MOC_SOURCES}
	${TRAVERSO_RESOURCES}
	${JACK_STUB}
	${FLAC_STUB}
	${MAD_STUB}
	${LAME_STUB}
	${OGG_STUB}
	${VORBIS_STUB}
	${VORBISFILE_STUB}
	${VORBIS_ENC_STUB}
)

target_precompile_headers(traverso PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/precompile.h)
target_precompile_headers(traversocore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/precompile.h)
target_precompile_headers(traversosheetcanvas REUSE_FROM traversocore)
target_precompile_headers(traversoaudiofileio REUSE_FROM traversocore)
target_precompile_headers(traversoaudiobackend REUSE_FROM traversocore)
target_precompile_headers(tcp_traversocommands REUSE_FROM traversocore)
target_precompile_headers(traversoplugins REUSE_FROM traversocore)
target_precompile_headers(traversocommands REUSE_FROM traversocore)



TARGET_LINK_LIBRARIES(traverso
	${Qt6Widgets_LIBRARIES}
	${Qt6Xml_LIBRARIES}
	traversosheetcanvas
	traversocore
	traversoaudiofileio
	traversoaudiobackend
	traversoplugins
	tcp_traversocommands
	traversocommands
	samplerate
	wavpack
	mad
	${OGG_LIB}
	${VORBIS_LIB}
	${VORBIS_FILE_LIB}
	${VORBIS_ENC_LIB}
	${FLAC_LIB}
	# 	rt
)

IF(WIN32)
TARGET_LINK_LIBRARIES(traverso
	sndfile
	fftw3
	mp3lame
        brotlidec brotlicommon jbig lerc bz2 lzma deflate zstd tiff
        flac mpg123 opus samplerate
        harfbuzz graphite2 rpcrt4
        winmm
)
ADD_CUSTOM_COMMAND(TARGET traverso POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:traverso>
        $<TARGET_RUNTIME_DLLS:traverso>
        COMMAND_EXPAND_LISTS
)
ELSEIF(APPLE)
TARGET_LINK_LIBRARIES(traverso
	"-framework CoreFoundation"
	"-framework Accelerate"
	"-framework ApplicationServices"
	sndfile
	fftw3
	dl
)
ELSE(LINUX)
TARGET_LINK_LIBRARIES(traverso
	sndfile
	fftw3
	dl
)
ENDIF()


IF(HAVE_PORTAUDIO)
TARGET_LINK_LIBRARIES(traverso
	portaudio
)
ENDIF(HAVE_PORTAUDIO)

IF(HAVE_LILV)
TARGET_LINK_LIBRARIES(traverso
	${LIBLILV_LIBRARIES}
)
ENDIF(HAVE_LILV)

IF(HAVE_MP3_ENCODING)
TARGET_LINK_LIBRARIES(traverso
	${LAME_LIB}
)
ENDIF(HAVE_MP3_ENCODING)

IF(HAVE_ALSA)
TARGET_LINK_LIBRARIES(traverso
	asound
)
ENDIF(HAVE_ALSA)

IF(HAVE_JACK)
TARGET_LINK_LIBRARIES(traverso
	${JACK_LIB}
)
ENDIF(HAVE_JACK)

IF(HAVE_PIPEWIRE)
TARGET_LINK_LIBRARIES(traverso
	${PIPEWIRE_LIBRARIES}
)
ENDIF(HAVE_PIPEWIRE)

SET(EXE_NAME "traverso${TAG_VERSION}")

INSTALL(TARGETS ${EXE_NAME}
	RUNTIME DESTINATION bin)
