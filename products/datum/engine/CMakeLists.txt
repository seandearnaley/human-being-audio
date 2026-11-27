add_library(dt_engine STATIC
    include/dt/engine.h
    src/engine.cpp)

target_include_directories(dt_engine PUBLIC include)
target_compile_features(dt_engine PUBLIC cxx_std_20)
target_link_libraries(dt_engine PUBLIC dt_core hba_dsp PRIVATE dt_warnings)
hba_require_nonblocking(dt_engine)
