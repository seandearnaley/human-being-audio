add_library(pf_engine STATIC
    include/pf/engine.h
    src/fold.h
    src/pole_zero.h
    src/engine.cpp)

target_include_directories(pf_engine
    PUBLIC include
    PRIVATE src)
target_compile_features(pf_engine PUBLIC cxx_std_20)
target_link_libraries(pf_engine PUBLIC pf_core hba_dsp PRIVATE pf_warnings)
hba_require_nonblocking(pf_engine)

add_library(pf_engine_internal INTERFACE)
target_include_directories(pf_engine_internal INTERFACE src)
target_link_libraries(pf_engine_internal INTERFACE pf_engine)
