add_library(tg_core STATIC
    include/tg/chord_matrix.h
    include/tg/params.h
    include/tg/preset.h
    src/chord_matrix.cpp
    src/params.cpp
    src/preset.cpp)

target_include_directories(tg_core PUBLIC include)
target_compile_features(tg_core PUBLIC cxx_std_20)
target_link_libraries(tg_core PUBLIC hba_dsp hba_core PRIVATE tg_warnings)
