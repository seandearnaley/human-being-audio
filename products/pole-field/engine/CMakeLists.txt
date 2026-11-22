# Verify that Clang enforces [[clang::nonblocking]] on the audio path.
include(CheckCXXSourceCompiles)
block()
    set(CMAKE_REQUIRED_FLAGS "-Werror=function-effects")
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
    check_cxx_source_compiles("
        #if !__has_cpp_attribute(clang::nonblocking)
        #error nonblocking attribute unavailable
        #endif
        int value() [[clang::nonblocking]] { return 1; }
    " PF_HAS_NONBLOCKING)
    if(PF_HAS_NONBLOCKING)
        check_cxx_source_compiles("
            int* allocate() [[clang::nonblocking]] { return new int; }
        " PF_ACCEPTS_NONBLOCKING_ALLOCATION)
    endif()
endblock()
if(NOT PF_HAS_NONBLOCKING OR PF_ACCEPTS_NONBLOCKING_ALLOCATION)
    message(FATAL_ERROR "Pole Field requires Clang with enforced nonblocking checks.")
endif()

add_subdirectory("${PROJECT_SOURCE_DIR}/../../modules/dsp" "${PROJECT_BINARY_DIR}/studio-dsp")

add_library(pf_engine STATIC
    include/pf/engine.h
    src/control.h
    src/crossover.h
    src/decimator.h
    src/denormals.h
    src/fold.h
    src/limiter.h
    src/pole_zero.h
    src/svf.h
    src/engine.cpp)

target_include_directories(pf_engine
    PUBLIC include
    PRIVATE src)
target_compile_features(pf_engine PUBLIC cxx_std_20)
target_link_libraries(pf_engine PUBLIC pf_core hba_dsp PRIVATE pf_warnings)
target_compile_options(pf_engine PRIVATE -Wfunction-effects -Werror=function-effects)

add_library(pf_engine_internal INTERFACE)
target_include_directories(pf_engine_internal INTERFACE src)
target_link_libraries(pf_engine_internal INTERFACE pf_engine)
