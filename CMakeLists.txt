# Set the minimum required CMake version and project metadata
cmake_minimum_required(VERSION 3.25...4.0)

project(traverso
    VERSION 0.49.1
    LANGUAGES C CXX
)

# Define the version suffix used by the application source code
set(VERSION_SUFFIX "-git-01-11-2010")
set(TRAVERSO_FULL_VERSION "${PROJECT_VERSION}${VERSION_SUFFIX}")

# Include GNU standard installation directories for distribution layout compliance
include(GNUInstallDirs)

# Allow CMake to look inside the local 'cmake' directory for custom modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# ---------------------------------------------------------------------------
# 1. Platform-Specific Audio Backend Defaults
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
else() # Linux / Unix distributions
    set(DEFAULT_WANT_ALSA ON)
    set(DEFAULT_WANT_JACK ON)
    set(DEFAULT_WANT_PIPEWIRE ON)
    set(DEFAULT_WANT_PORTAUDIO ON)
    set(DEFAULT_WANT_COREAUDIO OFF)
    set(DEFAULT_WANT_LV2 ON)
    set(DEFAULT_WANT_SSE ON)
endif()

# ---------------------------------------------------------------------------
# 2. User Build Options (MUST BE DEFINED BEFORE LOADING DEPENDENCIES)
# ---------------------------------------------------------------------------
option(WANT_JACK "Include JACK Audio Connection Kit support" ${DEFAULT_WANT_JACK})
option(WANT_PIPEWIRE "Include PipeWire support" ${DEFAULT_WANT_PIPEWIRE})
option(WANT_ALSA "Include ALSA support" ${DEFAULT_WANT_ALSA})
option(WANT_PORTAUDIO "Include PortAudio support" ${DEFAULT_WANT_PORTAUDIO})
option(WANT_COREAUDIO "Include CoreAudio support (macOS)" ${DEFAULT_WANT_COREAUDIO})
option(WANT_LV2 "Include LV2 Plugin support" ${DEFAULT_WANT_LV2})
option(WANT_M4A_DECODE "Include M4A/AAC decoding support (faad2)" ON)
option(WANT_M4A_ENCODE "Include M4A/AAC encoding support (faac)" ON)
option(WANT_DEBUG "Debug build" OFF)
option(AUTOPACKAGE_BUILD "Build traverso with autopackage tools" OFF)
option(WANT_THREAD_CHECK "Enable runtime thread checks for debugging" OFF)
option(DETECT_HOST_CPU_FEATURES "Detect the feature set of the host cpu" OFF)
option(WANT_SSE "Use SSE instruction set optimizations" ${DEFAULT_WANT_SSE})
option(ENABLE_ASAN "Enable AddressSanitizer for memory debugging (Development only)" OFF)

# Determine the dynamic default state for the command-line debugger capability
if(CMAKE_BUILD_TYPE MATCHES "[Dd]ebug" OR WANT_DEBUG)
    set(DEFAULT_TRAVERSO_DEBUG ON)
else()
    set(DEFAULT_TRAVERSO_DEBUG OFF)
endif()

option(WANT_TRAVERSO_DEBUG "Provides 4 levels of debug output on the command line" ${DEFAULT_TRAVERSO_DEBUG})

# ---------------------------------------------------------------------------
# 3. Centralized Interface Target Configuration (MUST BE BEFORE DEPENDENCIES)
# ---------------------------------------------------------------------------
add_library(traverso_compiler_settings INTERFACE)

target_compile_features(traverso_compiler_settings INTERFACE cxx_std_23)

target_compile_definitions(traverso_compiler_settings INTERFACE
    VERSION="${TRAVERSO_FULL_VERSION}"
)

# Debian Policy compliance: Enforce STATIC_BUILD definition strictly on Windows architectures
if(WIN32)
    target_compile_definitions(traverso_compiler_settings INTERFACE STATIC_BUILD)
endif()

if(WANT_TRAVERSO_DEBUG)
    target_compile_definitions(traverso_compiler_settings INTERFACE USE_DEBUGGER)
endif()

# ---------------------------------------------------------------------------
# 4. Load External Dependencies
# ---------------------------------------------------------------------------
include(Dependencies)

# ---------------------------------------------------------------------------
# 5. Platform-Specific Paths and Flags
# ---------------------------------------------------------------------------
if(APPLE)
    if(EXISTS "/opt/homebrew")
        target_include_directories(traverso_compiler_settings INTERFACE "/opt/homebrew/include")
        target_link_directories(traverso_compiler_settings INTERFACE "/opt/homebrew/lib")
    elseif(EXISTS "/usr/local/include")
        target_include_directories(traverso_compiler_settings INTERFACE "/usr/local/include")
        target_link_directories(traverso_compiler_settings INTERFACE "/usr/local/lib")
    endif()
elseif(WIN32)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a;.lib")
    set(BUILD_SHARED_LIBS OFF)

    target_compile_definitions(traverso_compiler_settings INTERFACE MPG123_STATIC OPT_STATIC)
    target_link_options(traverso_compiler_settings INTERFACE "LINKER:-static" "-static-libgcc" "-static-libstdc++")
    list(PREPEND CMAKE_PREFIX_PATH "C:/msys64/ucrt64/qt6-static/")
endif()

# ---------------------------------------------------------------------------
# 6. Load Specialized Compiler Flags and Hardening Options
# ---------------------------------------------------------------------------
include(CompilerFlags)

# ---------------------------------------------------------------------------
# 7. Hardware Architecture Probing
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

if(UNIX AND WANT_SSE)
    if(CMAKE_CXX_LIBRARY_ARCHITECTURE MATCHES "(x86_64|amd64)")
        target_compile_definitions(traverso_compiler_settings INTERFACE SSE_OPTIMIZATIONS USE_X86_64_ASM ARCH_X86_64)
    elseif(CMAKE_CXX_LIBRARY_ARCHITECTURE MATCHES "(i386|i686|x86)")
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
# 8. Automatic Compilation Handling and Delegation
# ---------------------------------------------------------------------------
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

add_subdirectory(src)
