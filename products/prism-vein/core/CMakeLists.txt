add_library(pv_core STATIC
    include/pv/nonblocking.h
    include/pv/params.h
    include/pv/preset.h
    src/params.cpp
    src/preset.cpp)

target_include_directories(pv_core PUBLIC include)
target_compile_features(pv_core PUBLIC cxx_std_20)
target_link_libraries(pv_core PUBLIC hba_dsp hba_core nlohmann_json::nlohmann_json PRIVATE pv_warnings)
