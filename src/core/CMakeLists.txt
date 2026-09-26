ENABLE_LANGUAGE(ASM)

SET(CORE_ASM_SOURCE "")
IF(HOST_SUPPORTS_SSE)
    IF(IS_ARCH_X86)
        SET(CORE_ASM_SOURCE ${CMAKE_SOURCE_DIR}/src/common/sse_functions.S)
    ENDIF()
    IF(IS_ARCH_X86_64)
        SET(CORE_ASM_SOURCE ${CMAKE_SOURCE_DIR}/src/common/sse_functions_64bit.S)
    ENDIF()
ENDIF()

IF(CORE_ASM_SOURCE)
    SET_SOURCE_FILES_PROPERTIES(${CORE_ASM_SOURCE} PROPERTIES LANGUAGE ASM)
ENDIF()

ADD_LIBRARY(traversocore STATIC
    ${CMAKE_SOURCE_DIR}/src/common/Utils.cpp
    ${CMAKE_SOURCE_DIR}/src/common/TAudioThreadMessageQueue.cpp
    ${CMAKE_SOURCE_DIR}/src/common/Debugger.cpp
    ${CMAKE_SOURCE_DIR}/src/common/Mixer.cpp
    ${CMAKE_SOURCE_DIR}/src/common/TTimeRef.cpp
    ${CMAKE_SOURCE_DIR}/src/common/TTransportControl.cpp
    TAudioClip.cpp
    TAudioClipGroup.cpp
    TAudioClipManager.cpp
    TAudioFileCopyConvert.cpp
    TAudioFileMerger.cpp
    TAudioTrack.cpp
    TBufferedAudioStream.cpp
    TCommand.cpp
    TConfig.cpp
    TShortCutManager.cpp
    TContextPointer.cpp
    TContextItem.cpp
    TCurve.cpp
    TCurveNode.cpp
    TDiskIOThread.cpp
    TFadeCurve.cpp
    FileHelpers.cpp
    TInformUser.cpp
    TInputEventDispatcher.cpp
    TPeak.cpp
    TProject.cpp
    TProjectManager.cpp
    TAudioProcessingNode.cpp
    TBufferedAudioStreamReader.cpp
    TResourcesManager.cpp
    TBusTrack.cpp
    TSend.cpp
    TSession.cpp
    TSheet.cpp
    TTrack.cpp
    TBufferedAudioStreamWriter.cpp
    gdither.cpp
    TSnapList.cpp
    TLocation.cpp
    TTimeLineRuler.cpp
    TTimeLineMarker.cpp
    TThemer.cpp
    TProjectConverter.cpp
    TGlobalContext.cpp
    TTransport.cpp
    TShortCutFunction.cpp
    TShortCut.cpp
    TExportSpecification.cpp
    TAudioResampler.cpp
    TCommandPlugin.h
    ${CORE_ASM_SOURCE}
)

target_include_directories(traversocore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src/audiofileio
        ${CMAKE_SOURCE_DIR}/src/audiofileio/decode
        ${CMAKE_SOURCE_DIR}/src/audiofileio/encode
        ${CMAKE_SOURCE_DIR}/src/common
        ${CMAKE_SOURCE_DIR}/src/commands
        ${CMAKE_SOURCE_DIR}/src/engine
        ${CMAKE_SOURCE_DIR}/src/plugins
        ${CMAKE_SOURCE_DIR}/src/plugins/native
)

target_link_libraries(traversocore
    PUBLIC
        Qt6::Widgets
        Qt6::Gui
        Qt6::Xml
        Qt6::Core
        Qt6::Concurrent
        PkgConfig::SOXR
)
