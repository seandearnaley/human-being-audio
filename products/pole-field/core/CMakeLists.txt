add_library(pf_core STATIC
    include/pf/cubes.h
    include/pf/curve.h
    include/pf/lanes.h
    include/pf/nonblocking.h
    include/pf/params.h
    include/pf/preset.h
    src/cubes.cpp
    src/params.cpp
    src/preset.cpp)
target_include_directories(pf_core PUBLIC include)
target_compile_features(pf_core PUBLIC cxx_std_20)
target_link_libraries(pf_core PUBLIC hba_dsp hba_core PRIVATE pf_warnings)
