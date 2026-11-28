add_library(lg_engine STATIC
    include/lg/engine.h
    include/lg/triple_buffer.h
    src/engine.cpp
    src/sketch_synth.h)

target_include_directories(lg_engine PUBLIC include)
target_compile_features(lg_engine PUBLIC cxx_std_20)
target_link_libraries(lg_engine PUBLIC lg_core hba_dsp PRIVATE lg_warnings)
hba_require_nonblocking(lg_engine)
