add_library(tg_engine STATIC
    include/tg/engine.h
    include/tg/export.h
    src/engine.cpp
    src/export.cpp
    src/modes.cpp
    src/mpe_output.cpp)

target_include_directories(tg_engine PUBLIC include)
target_compile_features(tg_engine PUBLIC cxx_std_20)
target_link_libraries(tg_engine PUBLIC tg_core hba_dsp PRIVATE tg_warnings)
hba_require_nonblocking(tg_engine)
