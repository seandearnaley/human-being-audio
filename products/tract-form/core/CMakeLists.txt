add_library(tf_core STATIC
    include/tf/curve.h
    include/tf/feel.h
    include/tf/lanes.h
    include/tf/params.h
    include/tf/preset.h
    include/tf/vowels.h
    src/params.cpp
    src/preset.cpp)
target_include_directories(tf_core PUBLIC include)
target_compile_features(tf_core PUBLIC cxx_std_20)
target_link_libraries(tf_core PUBLIC hba_dsp hba_core PRIVATE tf_warnings)
