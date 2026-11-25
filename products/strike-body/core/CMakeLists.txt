add_library(sb_core STATIC
    include/sb/modal_models.h
    include/sb/params.h
    include/sb/preset.h
    src/params.cpp
    src/preset.cpp)

target_include_directories(sb_core PUBLIC include)
target_compile_features(sb_core PUBLIC cxx_std_20)
target_link_libraries(sb_core PUBLIC hba_dsp hba_core PRIVATE sb_warnings)
