#Require 3.1.0 for Qt finding
CMAKE_MINIMUM_REQUIRED(VERSION 3.16.0 FATAL_ERROR)

IF(APPLE)
        SET(CMAKE_C_COMPILER clang CACHE FILEPATH "C compiler" FORCE)
        SET(CMAKE_CXX_COMPILER clang++ CACHE FILEPATH "C++ compiler" FORCE)
        SET(CMAKE_OSX_DEPLOYMENT_TARGET 10.15 CACHE STRING "Minimum macOS version" FORCE)
ENDIF()

#PROJECT SETUP
PROJECT(traverso)
SET (VERSION_MAJOR "0")
SET (VERSION_MINOR "49")
SET (VERSION_PATCH "1")
SET (VERSION_SUFFIX "-git-01-11-2010")
SET (VERSION ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}${VERSION_SUFFIX})

# Options with platform defaults
if(APPLE)
    set(DEFAULT_WANT_ALSA OFF)
    set(DEFAULT_WANT_JACK OFF)
    set(DEFAULT_WANT_PIPEWIRE OFF)
    set(DEFAULT_WANT_PORTAUDIO OFF)
    set(DEFAULT_WANT_COREAUDIO ON)
    set(DEFAULT_WANT_LV2 ON)
    set(DEFAULT_WANT_SSE OFF)
elseif(WIN32)
    set(DEFAULT_WANT_ALSA OFF)
    set(DEFAULT_WANT_JACK OFF)
    set(DEFAULT_WANT_PIPEWIRE OFF)
    set(DEFAULT_WANT_PORTAUDIO ON)
    set(DEFAULT_WANT_COREAUDIO OFF)
    set(DEFAULT_WANT_LV2 OFF)
    set(DEFAULT_WANT_SSE OFF)
else()
    set(DEFAULT_WANT_ALSA ON)
    set(DEFAULT_WANT_JACK ON)
    set(DEFAULT_WANT_PIPEWIRE ON)
    set(DEFAULT_WANT_PORTAUDIO OFF)
    set(DEFAULT_WANT_COREAUDIO OFF)
    set(DEFAULT_WANT_LV2 ON)
    set(DEFAULT_WANT_SSE ON)
endif()


# At release set this on:
#LIST(APPEND TRAVERSO_DEFINES -DQT_NO_DEBUG)

# Qt6 setup
if(APPLE)
    if(EXISTS "/opt/homebrew/opt/qtbase")
        list(PREPEND CMAKE_PREFIX_PATH "/opt/homebrew/opt/qtbase")
    elseif(EXISTS "/usr/local/opt/qtbase")
        list(PREPEND CMAKE_PREFIX_PATH "/usr/local/opt/qtbase")
    endif()

    if(EXISTS "/opt/homebrew/opt/qtsvg")
        list(PREPEND CMAKE_PREFIX_PATH "/opt/homebrew/opt/qtsvg")
    elseif(EXISTS "/usr/local/opt/qtsvg")
        list(PREPEND CMAKE_PREFIX_PATH "/usr/local/opt/qtsvg")
    endif()

    if(APPLE)
        set(Qt6Svg_DIR "/opt/homebrew/opt/qtsvg/lib/cmake/Qt6Svg" CACHE PATH "Path to Qt6Svg config" FORCE)
    endif()
elseif(WIN32)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a;.lib")
    set(BUILD_SHARED_LIBS OFF)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static -static-libgcc -static-libstdc++")
    list(PREPEND CMAKE_PREFIX_PATH "C:/msys64/ucrt64/qt6-static/")
    add_compile_options(-DMPG123_STATIC -DOPT_STATIC)
endif()

option(WANT_JACK "Include JACK Audio Connection Kit support" ${DEFAULT_WANT_JACK})
option(WANT_PIPEWIRE "Include PipeWire support" ${DEFAULT_WANT_PIPEWIRE})
option(WANT_ALSA "Include ALSA support" ${DEFAULT_WANT_ALSA})
option(WANT_PORTAUDIO "Include PortAudio support" ${DEFAULT_WANT_PORTAUDIO})
option(WANT_COREAUDIO "Include CoreAudio support (macOS)" ${DEFAULT_WANT_COREAUDIO})
option(WANT_LV2 "Include LV2 Plugin support" ${DEFAULT_WANT_LV2})
option(WANT_M4A_DECODE "Include M4A/AAC decoding support (faad2)" ON)
option(WANT_M4A_ENCODE "Include M4A/AAC encoding support (faac)" ON)
option(WANT_PCH "Use precompiled headers" OFF)
OPTION(WANT_DEBUG "Debug build" ON)
OPTION(WANT_TRAVERSO_DEBUG "Provides 4 levels of debug ouput on the command line, always on for DEBUG builds" ON)
OPTION(AUTOPACKAGE_BUILD "Build traverso with autopackage tools" OFF)
option(WANT_THREAD_CHECK "Enable runtime thread checks for debugging" OFF)
OPTION(DETECT_HOST_CPU_FEATURES "Detect the feature set of the host cpu, and compile with an optimal set of compiler flags" OFF)
option(WANT_SSE "Use SSE instruction set optimizations (x86/x86_64 only)" ${DEFAULT_WANT_SSE})

#CMAKE SETUP
SET(CMAKE_COLOR_MAKEFILE ON)
SET(CMAKE_VERBOSE_MAKEFILE ON)
SET(CMAKE_INCLUDE_CURRENT_DIR TRUE)
SET(CMAKE_SKIP_RULE_DEPENDENCY TRUE)
SET(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/modules")

IF(COMMAND CMAKE_POLICY)
        CMAKE_POLICY(SET CMP0005 NEW)
	CMAKE_POLICY(SET CMP0003 NEW)
ENDIF(COMMAND CMAKE_POLICY)

SET(MAIN_DIR_NAME "src")
IF(TAG_VERSION)
  MESSAGE(STATUS "Previously selected version tag: ${TAG_VERSION}")
  SET(WANT_VERSIONING 1)
  SET(CACHED_VERSIONING 1)
ENDIF(TAG_VERSION)
IF (WANT_VERSIONING AND NOT CACHED_VERSIONING)
  IF (NOT CUSTOM_VERSIONTAG)
    SET(TAG_VERSION "-${VERSION}" CACHE TYPE STRING)
  ELSE (NOT CUSTOM_VERSIONTAG)
    SET(TAG_VERSION ${CUSTOM_VERSIONTAG} CACHE TYPE STRING)
  ENDIF (NOT CUSTOM_VERSIONTAG)
ENDIF (WANT_VERSIONING AND NOT CACHED_VERSIONING)

# Static build is perfectly fine now, targets are properly relinked when needed!
SET(TRAVERSO_DEFINES -DSTATIC_BUILD)

#BUILD SETUP
#None, Debug, Release, .. or custom ones
# IF(WANT_DEBUG)
#   SET(CMAKE_BUILD_TYPE DEBUG)
#   SET(WANT_TRAVERSO_DEBUG ON)
# ELSE(WANT_DEBUG)
#   SET(CMAKE_BUILD_TYPE RELEASE)
# #  LIST(APPEND TRAVERSO_DEFINES -DQT_NO_DEBUG)
# ENDIF(WANT_DEBUG)


IF(WANT_TRAVERSO_DEBUG)
    LIST(APPEND TRAVERSO_DEFINES -DUSE_DEBUGGER)
ENDIF(WANT_TRAVERSO_DEBUG)

# Moves the Traverso exec. to the root dir and libs to the root's lib dir.
SET(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_BINARY_DIR}/bin)
SET(LIBRARY_OUTPUT_PATH ${CMAKE_CURRENT_BINARY_DIR}/lib)


# Add Mac build paths for DarwinPorts / MacPorts
# These need to be defined up here, above the include checks
if(APPLE)
    if(EXISTS "/opt/homebrew/opt/qtbase")
        list(PREPEND CMAKE_PREFIX_PATH "/opt/homebrew/opt/qtbase")
    elseif(EXISTS "/usr/local/opt/qtbase")
        list(PREPEND CMAKE_PREFIX_PATH "/usr/local/opt/qtbase")
    endif()

    if(EXISTS "/opt/homebrew/opt/qtsvg")
        list(PREPEND CMAKE_PREFIX_PATH "/opt/homebrew/opt/qtsvg")
    elseif(EXISTS "/usr/local/opt/qtsvg")
        list(PREPEND CMAKE_PREFIX_PATH "/usr/local/opt/qtsvg")
    endif()

    if(APPLE)
        set(Qt6Svg_DIR "/opt/homebrew/opt/qtsvg/lib/cmake/Qt6Svg" CACHE PATH "Path to Qt6Svg config" FORCE)
    endif()
    INCLUDE_DIRECTORIES("/opt/homebrew/include")
    SET (CMAKE_REQUIRED_INCLUDES "/opt/homebrew/include")
    LINK_DIRECTORIES("/opt/homebrew/lib")
elseif(WIN32)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a;.lib")
    set(BUILD_SHARED_LIBS OFF)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static -static-libgcc -static-libstdc++")
    list(PREPEND CMAKE_PREFIX_PATH "C:/msys64/ucrt64/qt6-static/")
    add_compile_options(-DMPG123_STATIC -DOPT_STATIC)
endif()

SET(MINIMAL_ALSA_VERSION 1.0.0)
SET(MINIMAL_JACK_VERSION 0.100)
SET(MINIMAL_PORTAUDIO_VERSION 19)
SET(MINIMAL_SLV2_VERSION 0.6.1)
SET(MINIMAL_WAVPACK_VERSION 5.6.0)
SET(MINIMAL_OGG_VORBIS_VERSION 1.1.2)
SET(MINIMAL_MAD_VERSION 0.15.0)
SET(MINIMAL_FLAC_VERSION 1.1.2)
SET(MINIMAL_FFTW_VERSION 3.0.0)
SET(MINIMAL_REDLAND_VERSION 1.0.2)
SET(MINIMAL_PULSAUDIO_VERSION 0.9)
SET(HAVE_COREAUDIO FALSE)
SET(HOST_SUPPORTS_SSE FALSE)
# set(CMAKE_INTERPROCEDURAL_OPTIMIZATION FALSE)

if(WIN32)
    SET(CMAKE_CXX_FLAGS_DEBUG "-O0 -ggdb3 -Wall -Wextra -Wno-unused-local-typedefs ${CXX_FLAGS}")
else()
    SET(CMAKE_CXX_FLAGS_DEBUG "-O0 -ggdb3 -Wall -Wextra -Wno-unused-local-typedefs -fsanitize=address ${CXX_FLAGS}")
endif()

SET(CMAKE_CXX_FLAGS_RELEASE "-O2 -Wall -Wno-unused-local-typedefs  ${CXX_FLAGS}")

# Detect the flags to be used for large file support
execute_process(
        COMMAND getconf LFS_CFLAGS
        OUTPUT_VARIABLE getconf_invoke_result
        RESULT_VARIABLE getconf_failed
)

# remove linebrakes, white spaces (getconf_invoke_result)
string(REGEX REPLACE "[\r\n]"                  " " getconf_invoke_result "${getconf_invoke_result}")

# now we have the proper flags for large file support, so set these!
SET(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} ${getconf_invoke_result}")
SET(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${getconf_invoke_result}")


INCLUDE (${CMAKE_ROOT}/Modules/TestBigEndian.cmake)
TEST_BIG_ENDIAN(WORDS_BIGENDIAN)


# Check for PKG-CONFIG
FIND_PACKAGE(PkgConfig)

#FIND DEPENDENCIES

INCLUDE (ConfigureChecks.cmake)


IF(WANT_PORTAUDIO)
        CHECK_INCLUDE_FILE("portaudio.h" HAVE_PORTAUDIO_H)
        IF(NOT HAVE_PORTAUDIO_H)
                MESSAGE(FATAL_ERROR "PortAudio support requested, but PortAudio development headers could not be found!\nPlease install the PortAudio development package, remove CMakeCache.txt and run cmake again")
        ENDIF(NOT HAVE_PORTAUDIO_H)
        IF(PKG_CONFIG_FOUND)
            PKG_CHECK_MODULES(PORTAUDIO portaudio-2.0>=${MINIMAL_PORTAUDIO_VERSION})
            IF(PORTAUDIO_FOUND)
                    LIST(APPEND TRAVERSO_DEFINES -DPORTAUDIO_SUPPORT)
                    MESSAGE("-- PortAudio Library Found OK")
                    SET(HAVE_PORTAUDIO TRUE)
            ENDIF(PORTAUDIO_FOUND)
        ENDIF(PKG_CONFIG_FOUND)
ELSE(WANT_PORTAUDIO)
                SET(HAVE_PORTAUDIO FALSE)
ENDIF(WANT_PORTAUDIO)

IF(WANT_LV2)
	IF(PKG_CONFIG_FOUND)
		PKG_CHECK_MODULES(LIBLILV lilv-0>=0.4.4)
		IF (LIBLILV_FOUND)
		    MESSAGE("-- LILV Library Found OK")
		    INCLUDE_DIRECTORIES(${LIBLILV_INCLUDE_DIRS})
		    SET (LV2_SUPPORT TRUE)
		    SET(HAVE_LILV TRUE)
                    LIST(APPEND TRAVERSO_DEFINES -DLV2_SUPPORT)
		ELSE (LIBLILV_FOUND)
		    MESSAGE("LILV support disabled")
		    SET (LV2_SUPPORT FALSE)
		    SET(HAVE_LILV FALSE)
		ENDIF (LIBLILV_FOUND)
                SET(SLV2OPTIONS "${HAVE_LILV}")
        ENDIF(PKG_CONFIG_FOUND)
ELSE(WANT_LV2)
	SET(HAVE_LILV FALSE)
	SET (LV2_SUPPORT FALSE)
ENDIF(WANT_LV2)


CHECK_INCLUDE_FILE("wavpack/wavpack.h" HAVE_WAVPACK_H)
IF(NOT HAVE_WAVPACK_H)
        MESSAGE(FATAL_ERROR "WavPack development headers could not be found!\nPlease install the WavPack development package, remove CMakeCache.txt and run cmake again")
ENDIF(NOT HAVE_WAVPACK_H)
IF (PKG_CONFIG_FOUND)
    PKG_CHECK_MODULES(WAVPACK  wavpack>=${MINIMAL_WAVPACK_VERSION})
    IF (WAVPACK_FOUND)
      MESSAGE("-- WavPack Library Found OK")
    ELSE(WAVPACK_FOUND)
      MESSAGE(SEND_ERROR "No WavPack found (WavPack >= ${MINIMAL_WAVPACK_VERSION} needs to be installed!)")
    ENDIF(WAVPACK_FOUND)
ENDIF(PKG_CONFIG_FOUND)


CHECK_INCLUDE_FILE("fftw3.h" HAVE_FFTW3_H)
IF(NOT HAVE_FFTW3_H)
        MESSAGE(FATAL_ERROR "FFTW3 development headers could not be found!\nPlease install the FFTW3 development package (fftw3-dev), remove CMakeCache.txt and run cmake again")
ENDIF(NOT HAVE_FFTW3_H)


CHECK_INCLUDE_FILE("sys/vfs.h" HAVE_SYS_VFS_H)
IF(HAVE_SYS_VFS_H)
        LIST(APPEND TRAVERSO_DEFINES -DHAVE_SYS_VFS_H)
ENDIF(HAVE_SYS_VFS_H)

CHECK_INCLUDE_FILE("sys/stat.h" HAVE_SYS_STAT_H)
IF(HAVE_SYS_STAT_H)
        LIST(APPEND TRAVERSO_DEFINES -DHAVE_SYS_STAT_H)
ENDIF(HAVE_SYS_STAT_H)


#Create configure files.. config.h
CONFIGURE_FILE(${CMAKE_CURRENT_SOURCE_DIR}/config.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/src/config.h)


#uninstall
CONFIGURE_FILE(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake_uninstall.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
  IMMEDIATE @ONLY)

ADD_CUSTOM_TARGET(uninstall "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake")


set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

SET (QT_MIN_VERSION "6.5.0")
find_package(Qt6 COMPONENTS Core Widgets Xml Concurrent REQUIRED)
set(CMAKE_AUTOMOC ON)
SET(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

#Set up include dirs with all found packages
INCLUDE_DIRECTORIES(
${CMAKE_SOURCE_DIR}/src/common
${Qt6Widgets_INCLUDE_DIRS}
${Qt6Widgets_INCLUDES}
${Qt6Xml_INCLUDE_DIRS}
${Qt6Xml_INCLUDES}
)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")

CHECK_FUNCTION_EXISTS(posix_memalign HAVE_POSIX_MEMALIGN)
IF(NOT HAVE_POSIX_MEMALIGN)
        LIST(APPEND TRAVERSO_DEFINES -DNO_POSIX_MEMALIGN)
        MESSAGE("Did not find posix_memalign(), using malloc")
ENDIF(NOT HAVE_POSIX_MEMALIGN)


CHECK_FUNCTION_EXISTS(mlock HAVE_MLOCK)
IF(HAVE_MLOCK)
        LIST(APPEND TRAVERSO_DEFINES -DUSE_MLOCK)
ENDIF(HAVE_MLOCK)



IF(UNIX)

#BEGIN Mac OS X section
        IF(APPLE)
                # Detect if we want native Mac OS X support (CoreAudio)
                # and if we can build with it.
                IF(WANT_COREAUDIO)
                        SET(HAVE_COREAUDIO TRUE)
                        LIST(APPEND TRAVERSO_DEFINES -DCOREAUDIO_SUPPORT)
                ELSE(WANT_COREAUDIO)
                        SET(HAVE_COREAUDIO FALSE)
                ENDIF(WANT_COREAUDIO)

# @ MAC OS X builders, I've NO IDEA what this should be, please have a look at it
# It should be possible to create a working bundle with CPack using stuff
# defined here ....???
                SET( ENV{MACOSX_DEPLOYMENT_TARGET} 10.15 )
                SET( ENV{LD_PREBIND} 1 )
                SET( ENV{LD_PREBIND_ALLOW_OVERLAP} 1 )

#		Bundle setup
                SET(BUILD_MAC_BUNDLE 1)
                set(MACOSX_BUNDLE_ICON_FILE ${PROJECT_SOURCE_DIR}/resources/images/traverso_mac.icns)

                LINK_LIBRARIES( "-framework CoreFoundation" )
        ENDIF(APPLE)
# END Mac OS X section


#BEGIN LINUX section
        IF(WANT_ALSA)
                CHECK_INCLUDE_FILE("alsa/asoundlib.h" HAVE_ASOUNDLIB_H)
                IF(NOT HAVE_ASOUNDLIB_H)
                        MESSAGE(FATAL_ERROR "alsa support requested, but alsa development headers could not be found!\nPlease install the alsa development package (Usually called libasound-dev), remove CMakeCache.txt and run cmake again")
                ENDIF(NOT HAVE_ASOUNDLIB_H)

                IF(PKG_CONFIG_FOUND)
                    PKG_CHECK_MODULES(ALSA  alsa>=${MINIMAL_ALSA_VERSION})
                    IF (ALSA_FOUND)
                            MESSAGE("-- ALSA Library Found OK")
                            LIST(APPEND TRAVERSO_DEFINES -DALSA_SUPPORT)
                            SET(HAVE_ALSA TRUE)
                    ENDIF(ALSA_FOUND)
                ENDIF(PKG_CONFIG_FOUND)
        ELSE(WANT_ALSA)
                MESSAGE("-- ALSA support: Disabled")
                SET(HAVE_ALSA FALSE)
        ENDIF(WANT_ALSA)

	PKG_CHECK_MODULES(SNDFILE  sndfile>=1.1.0)
	IF (SNDFILE_FOUND)
	        MESSAGE("-- SNDFILE Library Found OK")
	ENDIF(SNDFILE_FOUND)

# END LINUX section


        IF(WANT_JACK)
                CHECK_INCLUDE_FILE("jack/jack.h" HAVE_JACK_H)
                IF(NOT HAVE_JACK_H)
                        MESSAGE(FATAL_ERROR "jack support requested, but jack development headers could not be found!\nPlease install the jack development package, remove CMakeCache.txt and run cmake again")
                ENDIF(NOT HAVE_JACK_H)

                IF(PKG_CONFIG_FOUND)
                    PKG_CHECK_MODULES(JACK jack>=${MINIMAL_JACK_VERSION})
                    IF(JACK_FOUND)
                            LIST (APPEND TRAVERSO_DEFINES -DJACK_SUPPORT)
                            MESSAGE("-- Jack Library Found OK")
                            SET(HAVE_JACK TRUE)
                            SET(JACK_LIBS ${JACK_LIBRARIES})
                            LIST(APPEND JACK_LIB_DIR ${JACK_LIBDIR} ${JACK_LIBRARY_DIRS})
                            LIST(APPEND JACK_INC_DIR ${JACK_INCLUDEDIR} ${JACK_INCLUDE_DIRS})

                            CHECK_INCLUDE_FILE("jack/session.h" HAVE_JACK_SESSION_H)
                            IF(HAVE_JACK_SESSION_H)
                                    MESSAGE("-- Jack session support found OK")
                            ENDIF(HAVE_JACK_SESSION_H)

                            IF(JACK_SESSION_FOUND)
                                    LIST(APPEND TRAVERSO_DEFINES -DJACK_SESSION_SUPPORT)
                            ENDIF(JACK_SESSION_FOUND)

                    ELSE(JACK_FOUND)
                            MESSAGE(FATAL_ERROR "jack support requested, but jack libraries could not be found!\nPlease install the jack runtime libraries, remove CMakeCache.txt and run cmake again")
                    ENDIF(JACK_FOUND)
                ENDIF(PKG_CONFIG_FOUND)
        ELSE(WANT_JACK)
                MESSAGE("-- Jack support: Disabled")
                SET(HAVE_JACK FALSE)
        ENDIF(WANT_JACK)

        IF(WANT_PIPEWIRE)
                IF(PKG_CONFIG_FOUND)
                    PKG_CHECK_MODULES(PIPEWIRE libpipewire-0.3)
                    IF(PIPEWIRE_FOUND)
                            LIST (APPEND TRAVERSO_DEFINES -DPIPEWIRE_SUPPORT)
                            MESSAGE("-- PipeWire Library Found OK")
                            SET(HAVE_PIPEWIRE TRUE)
                            SET(PIPEWIRE_LIBS ${PIPEWIRE_LIBRARIES})
                            LIST(APPEND PIPEWIRE_LIB_DIR ${PIPEWIRE_LIBDIR} ${PIPEWIRE_LIBRARY_DIRS})
                            LIST(APPEND PIPEWIRE_INC_DIR ${PIPEWIRE_INCLUDEDIR} ${PIPEWIRE_INCLUDE_DIRS})
                            INCLUDE_DIRECTORIES(${PIPEWIRE_INCLUDE_DIRS})
                    ELSE(PIPEWIRE_FOUND)
                            MESSAGE(FATAL_ERROR "PipeWire support requested, but libpipewire-0.3 could not be found!\nPlease install libpipewire-0.3 development package, remove CMakeCache.txt and run cmake again")
                    ENDIF(PIPEWIRE_FOUND)
                ENDIF(PKG_CONFIG_FOUND)
        ELSE(WANT_PIPEWIRE)
                MESSAGE("-- PipeWire support: Disabled")
                SET(HAVE_PIPEWIRE FALSE)
        ENDIF(WANT_PIPEWIRE)


        IF(NOT WANT_DEBUG AND DETECT_HOST_CPU_FEATURES)
#		Detect the architecture we are compiling on
                MESSAGE("\nDetecting host cpu features")
                execute_process(
                        COMMAND uname -m
                        OUTPUT_VARIABLE uname_invoke_result
                        RESULT_VARIABLE uname_failed
                )
# 		Get the cpu flags to add the most optimal optimization flags to the compiler
                execute_process(
                        COMMAND cat /proc/cpuinfo
                        COMMAND grep ^flags
                        OUTPUT_VARIABLE cat_invoke_result
                        RESULT_VARIABLE cat_failed
                )

                SET(HOST_SUPPORTS_SSE FALSE)

                IF(cat_invoke_result MATCHES sse)
                        MESSAGE("Host cpu is sse capable")
                        SET(HOST_SUPPORTS_SSE TRUE)
                        LIST(APPEND TRAVERSO_DEFINES -DSSE_OPTIMIZATIONS)
                ENDIF(cat_invoke_result MATCHES sse)

                IF(cat_invoke_result MATCHES 3dnow)
                        SET(CMAKE_CXX_FLAGS_RELEASE  "${CMAKE_CXX_FLAGS_RELEASE} -m3dnow")
                ENDIF(cat_invoke_result MATCHES 3dnow)

                IF(uname_invoke_result MATCHES i586)
                        MESSAGE("Host cpu is i586")
                        SET(CMAKE_CXX_FLAGS_RELEASE  "${CMAKE_CXX_FLAGS_RELEASE} -march=i586")
                ENDIF(uname_invoke_result MATCHES i586)

                IF(uname_invoke_result MATCHES i686)
                        MESSAGE("Host cpu is i686")
                        SET(CMAKE_CXX_FLAGS_RELEASE  "${CMAKE_CXX_FLAGS_RELEASE} -march=i686")
                        IF(HOST_SUPPORTS_SSE)
                                SET(CMAKE_CXX_FLAGS_RELEASE  "${CMAKE_CXX_FLAGS_RELEASE} -msse -mfpmath=sse")
                                LIST(APPEND TRAVERSO_DEFINES -DUSE_XMMINTRIN)
                        ENDIF(HOST_SUPPORTS_SSE)
                ENDIF(uname_invoke_result MATCHES i686)

                IF(uname_invoke_result MATCHES x86_64)
                        MESSAGE("Host cpu is x86_64")
                        LIST(APPEND TRAVERSO_DEFINES -DARCH_X86)
                        IF(HOST_SUPPORTS_SSE)
                                SET(CMAKE_CXX_FLAGS_RELEASE  "${CMAKE_CXX_FLAGS_RELEASE} -msse -mfpmath=sse")
                                LIST(APPEND TRAVERSO_DEFINES -DUSE_XMMINTRIN -DUSE_X86_64_ASM)
                                SET(IS_ARCH_X86_64 TRUE)
                        ENDIF(HOST_SUPPORTS_SSE)
                ENDIF(uname_invoke_result MATCHES x86_64)

                IF(uname_invoke_result MATCHES i[456]86)
                        LIST(APPEND TRAVERSO_DEFINES -DARCH_X86)
                        SET(IS_ARCH_X86 TRUE)
                ENDIF(uname_invoke_result MATCHES i[456]86)

            ELSE(NOT WANT_DEBUG AND DETECT_HOST_CPU_FEATURES)
                IF(WANT_SSE)
                    MESSAGE("SSE wanted, setting compiler flags to -msse -mfpmath=sse")
                    MESSAGE("Detecting host architecture")
                    SET(CMAKE_CXX_FLAGS_RELEASE  "${CMAKE_CXX_FLAGS_RELEASE} -msse -mfpmath=sse")
                    LIST(APPEND TRAVERSO_DEFINES -DSSE_OPTIMIZATIONS -DUSE_XMMINTRIN)

                    execute_process(
                        COMMAND uname -m
                        OUTPUT_VARIABLE uname_invoke_result
                        RESULT_VARIABLE uname_failed
                    )

                    IF(uname_invoke_result MATCHES i[456]86)
                            LIST(APPEND TRAVERSO_DEFINES -DARCH_X86)
                    SET(IS_ARCH_X86 TRUE)
                    MESSAGE("-- Architecture is ARCH_X86")
                ENDIF(uname_invoke_result MATCHES i[456]86)

                    IF(uname_invoke_result MATCHES x86_64)
                            LIST(APPEND TRAVERSO_DEFINES -DUSE_X86_64_ASM -DARCH_X86_64)
                            SET(IS_ARCH_X86_64 TRUE)
                            MESSAGE("--Architecture is ARCH_X86_64")
                    ENDIF(uname_invoke_result MATCHES x86_64)
                    SET(HOST_SUPPORTS_SSE TRUE)
                ENDIF(WANT_SSE)
        ENDIF(NOT WANT_DEBUG AND DETECT_HOST_CPU_FEATURES)

ELSE(UNIX)

# Windows section
        IF(WIN32)
                LINK_LIBRARIES(
                    ${QT_LIBRARIES}
                    ${QT_QTXML_LIBRARY}
                    winmm
                )
                LIST (APPEND TRAVERSO_DEFINES -DQT_DLL -DWIN_BUILD -DPORTAUDIO_SUPPORT)
                SET(CMAKE_CXX_FLAGS_RELEASE "-O2 -Wall -fno-strict-aliasing ${CXX_FLAGS}")
                CHECK_INCLUDE_FILE(windows.h HAVE_WINDOWS_H)
                SET(HAVE_PORTAUDIO TRUE)
        ENDIF(WIN32)


ENDIF(UNIX)

macro(traverso_add_definition def)
    list(APPEND TRAVERSO_DEFINES_LIST "${def}")
    add_compile_definitions(${def})
endmacro()

set(HAVE_M4A_DECODING FALSE)
if(WANT_M4A_DECODE)
    pkg_check_modules(FAAD2 IMPORTED_TARGET faad2)
    if(FAAD2_FOUND)
        set(HAVE_M4A_DECODING TRUE)
        traverso_add_definition(M4A_DECODE_SUPPORT)
    else()
        message(FATAL_ERROR "M4A decoding requested but faad2 not found")
    endif()
endif()

set(HAVE_M4A_ENCODING FALSE)
if(WANT_M4A_ENCODE)
    pkg_check_modules(FAAC IMPORTED_TARGET faac)
    if(FAAC_FOUND)
        set(HAVE_M4A_ENCODING TRUE)
        traverso_add_definition(M4A_ENCODE_SUPPORT)
    else()
        message(FATAL_ERROR "M4A encoding requested but faac not found")
    endif()
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(SOXR REQUIRED IMPORTED_TARGET soxr)

ADD_DEFINITIONS(${QT_DEFINITIONS})
ADD_DEFINITIONS(${TRAVERSO_DEFINES})
SET(TRAVERSO_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR}/buildfiles)

#Add our source subdirs
ADD_SUBDIRECTORY(src)


# TODO
#Install our READMEs etc.
# INSTALL(FILES
# AUTHORS
# README
#
#   DESTINATION ${CMAKE_SOURCE_DIR}/src/
# )


# If the cmake version includes cpack, use it
IF(EXISTS "${CMAKE_ROOT}/Modules/CPack.cmake")
  SET(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Traverso is an open source Digital Audio Workstation for Linux, Mac OSX and Windows")
  SET(CPACK_PACKAGE_VENDOR "Traverso Team")
  SET(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/README")
  SET(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/README")
  SET(CPACK_PACKAGE_VERSION_MAJOR "${VERSION_MAJOR}")
  SET(CPACK_PACKAGE_VERSION_MINOR "${VERSION_MINOR}")
  SET(CPACK_PACKAGE_VERSION_PATCH "${VERSION_PATCH}")
  SET(CPACK_PACKAGE_INSTALL_DIRECTORY "Traverso ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")
  SET(CPACK_PACKAGE_EXECUTABLES "traverso${TAG_VERSION}")
  SET(CPACK_SOURCE_PACKAGE_FILE_NAME "traverso-${VERSION}")
  SET(CPACK_SOURCE_GENERATOR TGZ)
## I'll make this prettier later - ignore any tarballs in the root
  SET(CPACK_SOURCE_IGNORE_FILES
        CMakeCache.txt
        "~$"
        "Makefile$"
        "\\\\.cvsignore$"
        "^${PROJECT_SOURCE_DIR}.*/*tar.*"
        "^${PROJECT_SOURCE_DIR}.*/precompile.h.gch"
        "^${PROJECT_SOURCE_DIR}.*/cmake_install.cmake"
        "^${PROJECT_SOURCE_DIR}/CPackConfig.cmake"
        "^${PROJECT_SOURCE_DIR}/CPackSourceConfig.cmake"
        "^${PROJECT_SOURCE_DIR}/make-autopackage.sh"
        "^${PROJECT_SOURCE_DIR}/_CPack_Packages/"
        "^${PROJECT_SOURCE_DIR}.*/CMakeFiles/"
        "^${PROJECT_SOURCE_DIR}.*/CVS/"
        "^${PROJECT_SOURCE_DIR}.*/bin/"
        "^${PROJECT_SOURCE_DIR}.*/build/"
        "^${PROJECT_SOURCE_DIR}.*/buildfiles/"
        "^${PROJECT_SOURCE_DIR}.*/lib/"
        "^${PROJECT_SOURCE_DIR}/debian/"
        "^${PROJECT_SOURCE_DIR}/autopackage/"
        "^${PROJECT_SOURCE_DIR}.*/CVSROOT/"
        "^${PROJECT_SOURCE_DIR}/doc/")
  INCLUDE(CPack)
ENDIF(EXISTS "${CMAKE_ROOT}/Modules/CPack.cmake")



# Mark the following optoins as advanced
MARK_AS_ADVANCED(
QT_X11_X11_LIBRARY
QT_X11_m_LIBRARY
QT_X11_Xext_LIBRARY
LIBRARY_OUTPUT_PATH
AUTOPACKAGE_BUILD
CMAKE_BACKWARDS_COMPATIBILITY
)


MESSAGE("
Traverso ${VERSION} Configuration Summary:
  Building in mode      : ${CMAKE_BUILD_TYPE}
  ALSA support          : ${HAVE_ALSA}
  Jack support          : ${HAVE_JACK}
  PipeWire support      : ${HAVE_PIPEWIRE}
  PortAudio support     : ${HAVE_PORTAUDIO}
  CoreAudio support     : ${HAVE_COREAUDIO}
  SLV2 support          : ${SLV2OPTIONS}
  M4A decode support    : ${HAVE_M4A_DECODING}
  M4A encode support    : ${HAVE_M4A_ENCODING}
  SSE Optimizations     : ${HOST_SUPPORTS_SSE}
")

MESSAGE(STATUS "Traverso ${VERSION} will be built to install into ${CMAKE_INSTALL_PREFIX}")
