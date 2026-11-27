add_library(pv_engine STATIC
    include/pv/engine.h
    src/engine.cpp)

target_include_directories(pv_engine PUBLIC include)
target_compile_features(pv_engine PUBLIC cxx_std_20)
target_link_libraries(pv_engine PUBLIC pv_core hba_dsp PRIVATE pv_warnings)
hba_require_nonblocking(pv_engine)
