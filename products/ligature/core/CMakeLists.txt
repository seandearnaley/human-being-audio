add_library(lg_core STATIC
    include/lg/compiled.h
    include/lg/document.h
    include/lg/generate.h
    include/lg/midi_file.h
    include/lg/nonblocking.h
    include/lg/palette.h
    include/lg/routes.h
    include/lg/song.h
    include/lg/theory.h
    src/compiled.cpp
    src/document.cpp
    src/generate.cpp
    src/midi_file.cpp
    src/palette.cpp
    src/routes.cpp
    src/song.cpp
    src/theory.cpp)

target_include_directories(lg_core PUBLIC include "${PROJECT_SOURCE_DIR}/../../modules/studio-ui")
target_compile_features(lg_core PUBLIC cxx_std_20)
target_link_libraries(lg_core PUBLIC hba_dsp hba_core nlohmann_json::nlohmann_json PRIVATE lg_warnings)
