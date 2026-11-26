add_library(sb_engine STATIC
    include/sb/engine.h
    include/sb/export.h
    include/sb/micro_room.h
    src/auto_align.cpp
    src/engine.cpp
    src/engine_helpers.h
    src/export.cpp
    src/voice.cpp)

target_include_directories(sb_engine PUBLIC include)
target_compile_features(sb_engine PUBLIC cxx_std_20)
target_link_libraries(sb_engine PUBLIC sb_core hba_dsp PRIVATE sb_warnings)

hba_require_nonblocking(sb_engine)
