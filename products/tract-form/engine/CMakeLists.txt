# Some older Clang toolchains accept the annotation without enforcing it.
include(CheckCXXSourceCompiles)
block()
    set(CMAKE_REQUIRED_FLAGS "-Werror=function-effects")
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
    check_cxx_source_compiles("
        #if !__has_cpp_attribute(clang::nonblocking)
        #error nonblocking attribute unavailable
        #endif
        int value() [[clang::nonblocking]] { return 1; }
    " TF_HAS_NONBLOCKING)
    if(TF_HAS_NONBLOCKING)
        check_cxx_source_compiles("
            int* allocate() [[clang::nonblocking]] { return new int; }
        " TF_ACCEPTS_NONBLOCKING_ALLOCATION)
    endif()
endblock()
if(NOT TF_HAS_NONBLOCKING OR TF_ACCEPTS_NONBLOCKING_ALLOCATION)
    message(FATAL_ERROR "Tract Form requires Clang with enforced nonblocking checks. See the studio README for the native toolchain.")
endif()

add_subdirectory("${PROJECT_SOURCE_DIR}/../../modules/dsp" "${PROJECT_BINARY_DIR}/studio-dsp")
add_library(tf_engine STATIC
    include/tf/engine.h
    include/tf/nonblocking.h
    include/tf/render.h
    include/tf/voice_pitch.h
    src/control.h
    src/crossover.h
    src/decimator.h
    src/denormals.h
    src/feedback_delay.h
    src/fold.h
    src/harmonic.h
    src/limiter.h
    src/squash.h
    src/svf.h
    src/tract.h
    src/voice.h
    src/engine.cpp
    src/render.cpp)

target_include_directories(tf_engine
    PUBLIC include
    PRIVATE src)
target_compile_features(tf_engine PUBLIC cxx_std_20)
target_link_libraries(tf_engine PUBLIC tf_core hba_dsp PRIVATE tf_warnings)

# The audio path is checked by the compiler: Engine::process and everything it
# calls must be provably non-blocking (no allocation, locks, throws, syscalls).
target_compile_options(tf_engine PRIVATE -Wfunction-effects -Werror=function-effects)

# Internal headers, for the unit tests of the individual DSP pieces.
add_library(tf_engine_internal INTERFACE)
target_include_directories(tf_engine_internal INTERFACE src)
target_link_libraries(tf_engine_internal INTERFACE tf_engine)
