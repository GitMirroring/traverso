INCLUDE_DIRECTORIES(
${CMAKE_SOURCE_DIR}/src/audiofileio/decode
${CMAKE_SOURCE_DIR}/src/audiofileio/encode
${CMAKE_SOURCE_DIR}/src/audiofileio
${CMAKE_SOURCE_DIR}/src/commands
${CMAKE_SOURCE_DIR}/src/common
${CMAKE_SOURCE_DIR}/src/engine
${CMAKE_SOURCE_DIR}/src/plugins
${CMAKE_SOURCE_DIR}/src/plugins/native
)

SET(TRAVERSO_CORE_SOURCES
${CMAKE_SOURCE_DIR}/src/common/Utils.cpp
${CMAKE_SOURCE_DIR}/src/common/Utils.h
${CMAKE_SOURCE_DIR}/src/common/TAudioThreadMessageQueue.cpp
${CMAKE_SOURCE_DIR}/src/common/TAudioThreadMessageQueue.h
${CMAKE_SOURCE_DIR}/src/common/Debugger.cpp
${CMAKE_SOURCE_DIR}/src/common/Debugger.h
${CMAKE_SOURCE_DIR}/src/common/Mixer.cpp
${CMAKE_SOURCE_DIR}/src/common/Mixer.h
${CMAKE_SOURCE_DIR}/src/common/TTimeRef.cpp
${CMAKE_SOURCE_DIR}/src/common/TTimeRef.h
${CMAKE_SOURCE_DIR}/src/common/TTransportControl.cpp
${CMAKE_SOURCE_DIR}/src/common/TTransportControl.h

TAudioClip.cpp TAudioClip.h
TAudioClipGroup.cpp TAudioClipGroup.h
TAudioClipManager.cpp TAudioClipManager.h
TAudioFileCopyConvert.cpp TAudioFileCopyConvert.h
TAudioFileMerger.cpp TAudioFileMerger.h
TAudioTrack.cpp TAudioTrack.h
TBufferedAudioStream.cpp TBufferedAudioStream.h
TBufferedAudioStreamStatus.h TBufferedAudioStreamStatus.h
TQueueBufferSlot.h
TViewPortInterface.h
TCommand.cpp TCommand.h
TConfig.cpp TConfig.h
TShortCutManager.cpp TShortCutManager.h
TContextPointer.cpp TContextPointer.h
TContextItem.cpp TContextItem.h
TCurve.cpp TCurve.h
TCurveNode.cpp TCurveNode.h
TCommandPlugin.h
TDiskIOThread.cpp TDiskIOThread.h
TFadeCurve.cpp TFadeCurve.h
FileHelpers.cpp FileHelpers.h
TInformUser.cpp TInformUser.h
TInputEventDispatcher.cpp TInputEventDispatcher.h
TPeak.cpp TPeak.h
TProject.cpp TProject.h
TProjectManager.cpp TProjectManager.h
TAudioProcessingNode.cpp TAudioProcessingNode.h
TBufferedAudioStreamReader.cpp TBufferedAudioStreamReader.h
TResourcesManager.cpp TResourcesManager.h
TBusTrack.cpp TBusTrack.h
TSend.cpp TSend.h
TSession.cpp TSession.h
TSheet.cpp TSheet.h
TTrack.cpp TTrack.h
TBufferedAudioStreamWriter.cpp TBufferedAudioStreamWriter.h
gdither.cpp gdither.h
TSnapList.cpp TSnapList.h
TLocation.cpp TLocation.h
TTimeLineRuler.cpp TTimeLineRuler.h
TTimeLineMarker.cpp TTimeLineMarker.h
TThemer.cpp TThemer.h
TProjectConverter.cpp TProjectConverter.h
TGlobalContext.cpp TGlobalContext.h
TTransport.cpp TTransport.h
TShortCutFunction.h TShortCutFunction.cpp
TShortCut.h TShortCut.cpp
TExportSpecification.h TExportSpecification.cpp
TAudioClipAddRemoveSpec.h
)

IF(UNIX)
	IF(HOST_SUPPORTS_SSE)
		IF(IS_ARCH_X86)
			SET(ASM_FILE ${CMAKE_SOURCE_DIR}/src/common/sse_functions.S)
		ENDIF(IS_ARCH_X86)
		IF(IS_ARCH_X86_64)
			SET(ASM_FILE ${CMAKE_SOURCE_DIR}/src/common/sse_functions_64bit.S)
		ENDIF(IS_ARCH_X86_64)
		
		add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/sse_functions.o
			COMMAND as ${ASM_FILE} -o ${CMAKE_CURRENT_BINARY_DIR}/sse_functions.o
			DEPENDS ${ASM_FILE}
		)
		SET(SSE_OBJECT_FILE ${CMAKE_CURRENT_BINARY_DIR}/sse_functions.o)
	ENDIF(HOST_SUPPORTS_SSE)
ENDIF(UNIX)


SET(TRAVERSO_CORE_LIBRARY "traversocore")

ADD_LIBRARY(${TRAVERSO_CORE_LIBRARY} STATIC ${TRAVERSO_CORE_SOURCES} ${SSE_OBJECT_FILE}
	TAudioResampler.h TAudioResampler.cpp)
target_link_libraries(traversocore PRIVATE Qt6::Concurrent)

IF(USE_PCH)
    ADD_DEPENDENCIES(traversocore precompiled_headers)
ENDIF(USE_PCH)
