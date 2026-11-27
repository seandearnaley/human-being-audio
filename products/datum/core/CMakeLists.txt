add_library(dt_core STATIC
    include/dt/nonblocking.h
    include/dt/params.h
    include/dt/pattern.h
    include/dt/preset.h
    src/params.cpp
    src/preset.cpp)

target_include_directories(dt_core PUBLIC include)
target_compile_features(dt_core PUBLIC cxx_std_20)
target_link_libraries(dt_core PUBLIC hba_dsp hba_core nlohmann_json::nlohmann_json PRIVATE dt_warnings)
