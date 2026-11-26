add_library(cl_core STATIC
    include/cl/curve.h
    include/cl/groove.h
    include/cl/groove_json.h
    include/cl/lanes.h
    include/cl/nonblocking.h
    include/cl/params.h
    include/cl/pattern.h
    include/cl/preset.h
    src/params.cpp
    src/groove_json.cpp
    src/groove_json_detail.h
    src/pattern.cpp
    src/preset.cpp)
target_include_directories(cl_core PUBLIC include)
target_compile_features(cl_core PUBLIC cxx_std_20)
target_link_libraries(cl_core PUBLIC hba_dsp hba_core PRIVATE cl_warnings)
