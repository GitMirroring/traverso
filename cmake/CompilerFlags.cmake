# ---------------------------------------------------------------------------
# Specialized Compiler and Linker Flags (Aligned with Debian Sid Production)
# ---------------------------------------------------------------------------

# Probe for Large File Support flags via system configuration
find_program(GETCONF_EXECUTABLE getconf)
if(GETCONF_EXECUTABLE)
    execute_process(
        COMMAND ${GETCONF_EXECUTABLE} LFS_CFLAGS
        OUTPUT_VARIABLE LFS_FLAGS
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
endif()

# Base warnings and Large File Support (Always active across all build profiles)
target_compile_options(traverso_compiler_settings INTERFACE -Wall -Wextra -Wno-unused-local-typedefs ${LFS_FLAGS})

# ---------------------------------------------------------------------------
# Debian Sid Production Hardening and LTO Optimization (STRICTLY RELEASE ONLY)
# ---------------------------------------------------------------------------
if(NOT CMAKE_BUILD_TYPE MATCHES "[Dd]ebug" AND NOT WANT_DEBUG)

    # 1. Debian Default Optimization Profile
    target_compile_options(traverso_compiler_settings INTERFACE -O2)

    # 2. Debian Security Hardening - Compiler Framework Guards
    target_compile_options(traverso_compiler_settings INTERFACE
        -g                          # Retain debug symbols for detached dbg symbol split-packages
        -fstack-protector-strong    # Proactive stack smashing protection (Debian packaging standard)
        -Wformat                    # Verify string format layouts for printf-style methods
        -Werror=format-security     # Reject dangerous format-string vulnerabilities at compile time
        -fPIE                       # Position Independent Executable to enable robust ASLR mitigation
    )

    # 3. Debian Security Hardening - Preprocessor Definitions
    target_compile_definitions(traverso_compiler_settings INTERFACE
        _FORTIFY_SOURCE=3           # Heavy buffer boundary runtime validation (GCC 14/15 default)
    )

    # 4. Debian Security Hardening - Linker Security Guards
    target_link_options(traverso_compiler_settings INTERFACE
        "LINKER:-z,relro"           # Enforce Read-Only Relocations to secure GOT assignment scopes
        "LINKER:-z,now"             # Trigger immediate binding to eliminate runtime GOT exploitation vectors
        "-pie"                      # Enforce dynamic address space randomization execution mapping
    )

    # 5. Link Time Optimization (LTO)
    include(CheckIPOSupported)

    check_ipo_supported(RESULT lto_supported OUTPUT error)
    if(lto_supported)
        # Actively enforce LTO parallel multi-core link bindings (matching -flto=auto)
        set_target_properties(traverso_compiler_settings PROPERTIES
            INTERFACE_INTERPROCEDURAL_OPTIMIZATION TRUE
        )
        # Apply the -ffat-lto-objects flag required by Debian packaging guidelines
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(traverso_compiler_settings INTERFACE -ffat-lto-objects)
        endif()
    endif()
endif()

# ---------------------------------------------------------------------------
# Optional AddressSanitizer Allocation Tracking (Development Diagnostics Only)
# ---------------------------------------------------------------------------
if(ENABLE_ASAN)
    target_compile_options(traverso_compiler_settings INTERFACE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(traverso_compiler_settings INTERFACE -fsanitize=address)
endif()
