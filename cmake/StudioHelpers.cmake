# Shared CMake helpers for Human Being Audio products (works with or without presets).

# Enable ccache for C, C++, and (on macOS) Objective-C/Objective-C++ when available.
macro(hba_enable_ccache)
    find_program(HBA_CCACHE ccache HINTS /opt/homebrew/bin /usr/local/bin)
    if(HBA_CCACHE AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
        set(CMAKE_C_COMPILER_LAUNCHER "${HBA_CCACHE}")
        set(CMAKE_CXX_COMPILER_LAUNCHER "${HBA_CCACHE}")
        if(APPLE)
            set(CMAKE_OBJC_COMPILER_LAUNCHER "${HBA_CCACHE}")
            set(CMAKE_OBJCXX_COMPILER_LAUNCHER "${HBA_CCACHE}")
        endif()
    endif()
endmacro()

# Verify Clang enforces [[clang::nonblocking]] and enable -Werror=function-effects on `target`.
function(hba_require_nonblocking target)
    if(NOT DEFINED HBA_NONBLOCKING_VERIFIED)
        include(CheckCXXSourceCompiles)
        block()
            set(CMAKE_REQUIRED_FLAGS "-Werror=function-effects")
            set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
            check_cxx_source_compiles("
                #if !__has_cpp_attribute(clang::nonblocking)
                #error nonblocking attribute unavailable
                #endif
                int value() [[clang::nonblocking]] { return 1; }
            " HBA_HAS_NONBLOCKING)
            if(HBA_HAS_NONBLOCKING)
                check_cxx_source_compiles("
                    int* allocate() [[clang::nonblocking]] { return new int; }
                " HBA_ACCEPTS_NONBLOCKING_ALLOCATION)
            endif()
        endblock()
        if(NOT HBA_HAS_NONBLOCKING OR HBA_ACCEPTS_NONBLOCKING_ALLOCATION)
            message(FATAL_ERROR "${PROJECT_NAME} requires Clang with enforced [[clang::nonblocking]] checks.")
        endif()
        set(HBA_NONBLOCKING_VERIFIED TRUE CACHE INTERNAL "Verified clang::nonblocking enforcement")
    endif()
    target_compile_options(${target} PRIVATE -Wfunction-effects -Werror=function-effects)
endfunction()

# Restrict developer/test/CLI executables to the host architecture when building
# macOS universal (arm64;x86_64) releases, avoiding compiling test targets twice.
function(hba_build_for_host_only target)
    if(APPLE AND CMAKE_OSX_ARCHITECTURES)
        list(LENGTH CMAKE_OSX_ARCHITECTURES _arch_count)
        if(_arch_count GREATER 1)
            set_target_properties(${target} PROPERTIES OSX_ARCHITECTURES "${CMAKE_HOST_SYSTEM_PROCESSOR}")
        endif()
    endif()
endfunction()
