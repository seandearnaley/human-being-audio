add_library(ln_core STATIC
    include/ln/curve.h
    include/ln/lanes.h
    include/ln/nonblocking.h
    include/ln/params.h
    include/ln/preset.h
    src/params.cpp
    src/preset.cpp)

target_include_directories(ln_core PUBLIC include)
target_compile_features(ln_core PUBLIC cxx_std_20)
target_link_libraries(ln_core PUBLIC hba_dsp hba_core PRIVATE nlohmann_json::nlohmann_json ln_warnings)
