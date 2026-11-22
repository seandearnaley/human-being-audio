# Verify that Clang enforces [[clang::nonblocking]] on the real-time path.
include(CheckCXXSourceCompiles)
block()
    set(CMAKE_REQUIRED_FLAGS "-Werror=function-effects")
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
    check_cxx_source_compiles("
        #if !__has_cpp_attribute(clang::nonblocking)
        #error nonblocking attribute unavailable
        #endif
        int value() [[clang::nonblocking]] { return 1; }
    " CL_HAS_NONBLOCKING)
    if(CL_HAS_NONBLOCKING)
        check_cxx_source_compiles("
            int* allocate() [[clang::nonblocking]] { return new int; }
        " CL_ACCEPTS_NONBLOCKING_ALLOCATION)
    endif()
endblock()
if(NOT CL_HAS_NONBLOCKING OR CL_ACCEPTS_NONBLOCKING_ALLOCATION)
    message(FATAL_ERROR "Cutline requires Clang with enforced nonblocking checks.")
endif()

add_library(cl_engine STATIC
    include/cl/engine.h
    include/cl/midi_event.h
    include/cl/phrase.h
    include/cl/smf.h
    src/audition.h
    src/engine.cpp
    src/phrase.cpp
    src/smf.cpp)

target_include_directories(cl_engine
    PUBLIC include
    PRIVATE src)
target_compile_features(cl_engine PUBLIC cxx_std_20)
target_link_libraries(cl_engine PUBLIC cl_core hba_dsp PRIVATE cl_warnings)
target_compile_options(cl_engine PRIVATE -Wfunction-effects -Werror=function-effects)

add_library(cl_engine_internal INTERFACE)
target_include_directories(cl_engine_internal INTERFACE src)
target_link_libraries(cl_engine_internal INTERFACE cl_engine)
