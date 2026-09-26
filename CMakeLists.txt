# Set the minimum required CMake version and project metadata
cmake_minimum_required(VERSION 3.25...4.0)

project(traverso
    VERSION 0.49.1
    LANGUAGES C CXX
)

# Define the version suffix used by the application source code
set(VERSION_SUFFIX "-git-01-11-2010")
set(TRAVERSO_FULL_VERSION "${PROJECT_VERSION}${VERSION_SUFFIX}")

# ---------------------------------------------------------------------------
# Platform-Specific Audio Backend Defaults
# ---------------------------------------------------------------------------
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET 10.15 CACHE STRING "Minimum macOS version" FORCE)

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
else() # Linux / Unix
    set(DEFAULT_WANT_ALSA ON)
    set(DEFAULT_WANT_JACK ON)
    set(DEFAULT_WANT_PIPEWIRE ON)
    set(DEFAULT_WANT_PORTAUDIO ON)
    set(DEFAULT_WANT_COREAUDIO OFF)
    set(DEFAULT_WANT_LV2 ON)
    set(DEFAULT_WANT_SSE ON)
endif()

# ---------------------------------------------------------------------------
# User Build Options
# ---------------------------------------------------------------------------
option(WANT_JACK "Include JACK Audio Connection Kit support" ${DEFAULT_WANT_JACK})
option(WANT_PIPEWIRE "Include PipeWire support" ${DEFAULT_WANT_PIPEWIRE})
option(WANT_ALSA "Include ALSA support" ${DEFAULT_WANT_ALSA})
option(WANT_PORTAUDIO "Include PortAudio support" ${DEFAULT_WANT_PORTAUDIO})
option(WANT_COREAUDIO "Include CoreAudio support (macOS)" ${DEFAULT_WANT_COREAUDIO})
option(WANT_LV2 "Include LV2 Plugin support" ${DEFAULT_WANT_LV2})
option(WANT_M4A_DECODE "Include M4A/AAC decoding support (faad2)" ON)
option(WANT_M4A_ENCODE "Include M4A/AAC encoding support (faac)" ON)
option(WANT_DEBUG "Debug build" ON)
option(WANT_TRAVERSO_DEBUG "Provides 4 levels of debug output on the command line" ON)
option(AUTOPACKAGE_BUILD "Build traverso with autopackage tools" OFF)
option(WANT_THREAD_CHECK "Enable runtime thread checks for debugging" OFF)
option(DETECT_HOST_CPU_FEATURES "Detect the feature set of the host cpu" OFF)
option(WANT_SSE "Use SSE instruction set optimizations" ${DEFAULT_WANT_SSE})

# ---------------------------------------------------------------------------
# Centralized Interface Target Configuration
# ---------------------------------------------------------------------------
# This interface target holds compiler settings, flags, and macro definitions
# to be inherited by all targets within the project subdirectories.
add_library(traverso_compiler_settings INTERFACE)

# Enforce the C++17 standard for all consumers
target_compile_features(traverso_compiler_settings INTERFACE cxx_std_17)

# Apply global preprocessor definitions
target_compile_definitions(traverso_compiler_settings INTERFACE
    STATIC_BUILD
    VERSION="${TRAVERSO_FULL_VERSION}"
)

if(WANT_TRAVERSO_DEBUG)
    target_compile_definitions(traverso_compiler_settings INTERFACE USE_DEBUGGER)
endif()

# ---------------------------------------------------------------------------
# Platform-Specific Paths and Flags
# ---------------------------------------------------------------------------
if(APPLE)
    target_include_directories(traverso_compiler_settings INTERFACE "/opt/homebrew/include")
    target_link_directories(traverso_compiler_settings INTERFACE "/opt/homebrew/lib")
elseif(WIN32)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a;.lib")
    set(BUILD_SHARED_LIBS OFF)

    target_compile_definitions(traverso_compiler_settings INTERFACE MPG123_STATIC OPT_STATIC)
    target_link_options(traverso_compiler_settings INTERFACE "LINKER:-static" "LINKER:-static-libgcc" "LINKER:-static-libstdc++")
    list(PREPEND CMAKE_PREFIX_PATH "C:/msys64/ucrt64/qt6-static/")
endif()

# ---------------------------------------------------------------------------
# Compiler and Linker Flags via Generator Expressions
# ---------------------------------------------------------------------------
# Query the system configuration tool for Large File Support flags
find_program(GETCONF_EXECUTABLE getconf)
if(GETCONF_EXECUTABLE)
    execute_process(
        COMMAND ${GETCONF_EXECUTABLE} LFS_CFLAGS
        OUTPUT_VARIABLE LFS_FLAGS
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
endif()

# Define profile-specific options for both Windows and Posix environments.
# For non-Windows Debug builds, AddressSanitizer (-fsanitize=address) is enabled.
# The Release profile appends standard diagnostics while preserving downstream package management flags.
if(WIN32)
    target_compile_options(traverso_compiler_settings INTERFACE
        $<$<CONFIG:Debug>:-O0 -ggdb3 -Wall -Wextra -Wno-unused-local-typedefs ${LFS_FLAGS}>
        $<$<CONFIG:Release>:-O2 -Wall -Wno-unused-local-typedefs ${LFS_FLAGS}>
    )
else()
    # Compilation flags for Linux/macOS
    target_compile_options(traverso_compiler_settings INTERFACE
        $<$<CONFIG:Debug>:-O0 -ggdb3 -Wall -Wextra -Wno-unused-local-typedefs -fsanitize=address ${LFS_FLAGS}>
        $<$<CONFIG:Release>:-Wall -Wextra -Wno-unused-local-typedefs ${LFS_FLAGS}>
    )

    # Linker flags for Linux/macOS to resolve ASan library symbols
    target_link_options(traverso_compiler_settings INTERFACE
        $<$<CONFIG:Debug>:-fsanitize=address>
    )
endif()

# ---------------------------------------------------------------------------
# Distribution-Compliant Hardware Architecture Probing
# ---------------------------------------------------------------------------
include(TestBigEndian)
test_big_endian(WORDS_BIGENDIAN)

include(CheckFunctionExists)
check_function_exists(posix_memalign HAVE_POSIX_MEMALIGN)
if(NOT HAVE_POSIX_MEMALIGN)
    target_compile_definitions(traverso_compiler_settings INTERFACE NO_POSIX_MEMALIGN)
endif()

check_function_exists(mlock HAVE_MLOCK)
if(HAVE_MLOCK)
    target_compile_definitions(traverso_compiler_settings INTERFACE USE_MLOCK)
endif()

# Architecture evaluation compatible with build daemons and cross-compilation environments
if(UNIX AND WANT_SSE)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|amd64|AMD64)")
        # x86_64 target guarantees SSE/SSE2 support on distribution environments
        target_compile_definitions(traverso_compiler_settings INTERFACE SSE_OPTIMIZATIONS USE_X86_64_ASM ARCH_X86_64)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "(i.86|x86)")
        # 32-bit x86 target requires verification to respect baseline standards
        include(CheckCXXSourceCompiles)

        set(SAFE_CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS}")
        set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -msse")

        check_cxx_source_compiles("
            #include <xmmintrin.h>
            int main() {
                __m128 a = _mm_setzero_ps();
                return 0;
            }
        " TARGET_SUPPORTS_SSE)

        set(CMAKE_REQUIRED_FLAGS "${SAFE_CMAKE_REQUIRED_FLAGS}")

        if(TARGET_SUPPORTS_SSE)
            target_compile_definitions(traverso_compiler_settings INTERFACE SSE_OPTIMIZATIONS ARCH_X86)
            target_compile_options(traverso_compiler_settings INTERFACE -msse)
        endif()
    endif()
endif()

# ---------------------------------------------------------------------------
# Dependency Resolution via PkgConfig and Native Modules
# ---------------------------------------------------------------------------
find_package(PkgConfig REQUIRED)

# Locate mandatory core dependencies and generate modern imported targets
pkg_check_modules(SOXR REQUIRED IMPORTED_TARGET soxr)
pkg_check_modules(SNDFILE REQUIRED IMPORTED_TARGET sndfile>=1.1.0)
pkg_check_modules(WAVPACK REQUIRED IMPORTED_TARGET wavpack>=5.6.0)
pkg_check_modules(FFTW3 REQUIRED IMPORTED_TARGET fftw3>=3.0.0)

# Locate mandatory audio decoders and processing libraries via pkg-config
pkg_check_modules(SAMPLERATE REQUIRED IMPORTED_TARGET samplerate)
pkg_check_modules(MAD REQUIRED IMPORTED_TARGET mad)

# Locate optional decoders and encoders
if(WANT_M4A_DECODE)
    pkg_check_modules(FAAD2 IMPORTED_TARGET faad2)
    if(FAAD2_FOUND)
        target_compile_definitions(traverso_compiler_settings INTERFACE M4A_DECODE_SUPPORT)
    endif()
endif()

if(WANT_M4A_ENCODE)
    pkg_check_modules(FAAC IMPORTED_TARGET faac)
    if(FAAC_FOUND)
        target_compile_definitions(traverso_compiler_settings INTERFACE M4A_ENCODE_SUPPORT)
    endif()
endif()

# Locate optional hardware backends and plugin formats
if(UNIX AND WANT_ALSA)
    pkg_check_modules(ALSA IMPORTED_TARGET alsa>=1.0.0)
    if(ALSA_FOUND)
        target_compile_definitions(traverso_compiler_settings INTERFACE ALSA_SUPPORT)
    endif()
endif()

if(UNIX AND WANT_JACK)
    pkg_check_modules(JACK IMPORTED_TARGET jack>=0.100)
    if(JACK_FOUND)
        target_compile_definitions(traverso_compiler_settings INTERFACE JACK_SUPPORT)
    endif()
endif()

if(UNIX AND WANT_PIPEWIRE)
    pkg_check_modules(PIPEWIRE IMPORTED_TARGET libpipewire-0.3)
    if(PIPEWIRE_FOUND)
        target_compile_definitions(traverso_compiler_settings INTERFACE PIPEWIRE_SUPPORT)
    endif()
endif()

if(WANT_PORTAUDIO)
    pkg_check_modules(PORTAUDIO IMPORTED_TARGET portaudio-2.0>=19)
    if(PORTAUDIO_FOUND)
        target_compile_definitions(traverso_compiler_settings INTERFACE PORTAUDIO_SUPPORT)
    endif()
endif()

if(WANT_LV2)
    pkg_check_modules(LIBLILV IMPORTED_TARGET lilv-0>=0.4.4)
    if(LIBLILV_FOUND)
        target_compile_definitions(traverso_compiler_settings INTERFACE LV2_SUPPORT)
    endif()
endif()

# Locate the required Qt6 framework components
find_package(Qt6 COMPONENTS Core Widgets Xml Concurrent REQUIRED)

# ---------------------------------------------------------------------------
# Output Directives and Subdirectory Delegation
# ---------------------------------------------------------------------------
set(CMAKE_EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_PATH ${CMAKE_CURRENT_BINARY_DIR}/lib)

# Enable automatic compilation handling for Qt MOC, UIC, and RCC files
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

add_subdirectory(src)
