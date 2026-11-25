add_library(sb_engine STATIC
    include/sb/engine.h
    include/sb/export.h
    src/engine.cpp
    src/export.cpp)

target_include_directories(sb_engine PUBLIC include)
target_compile_features(sb_engine PUBLIC cxx_std_20)
target_link_libraries(sb_engine PUBLIC sb_core hba_dsp PRIVATE sb_warnings)

hba_require_nonblocking(sb_engine)
