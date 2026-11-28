add_embedded_resources(lg_ui_brand "lg_brand.h" "lg::ui::brand"
    "${PROJECT_SOURCE_DIR}/brand/logo/ligature-logotype-on-dark.svg;${PROJECT_SOURCE_DIR}/brand/logo/ligature-mark-on-dark.svg")

add_library(lg_ui STATIC
    include/lg/ui/editor.h
    include/lg/ui/fake_model.h
    include/lg/ui/model.h
    src/control.h
    src/editor.cpp
    src/fake_model.cpp
    src/inspector.cpp
    src/inspector.h
    src/keyboard_lens.cpp
    src/keyboard_lens.h
    src/song_strip.cpp
    src/song_strip.h
    src/theme.cpp
    src/theme.h
    src/timeline.cpp
    src/timeline.h
    src/top_bar.cpp
    src/top_bar.h)

target_include_directories(lg_ui PUBLIC include)
target_compile_features(lg_ui PUBLIC cxx_std_20)
# The editor only reads the engine's telemetry types, so it takes the engine's headers rather than its library:
# the plugin links lg_engine itself.
target_include_directories(lg_ui PUBLIC "${PROJECT_SOURCE_DIR}/engine/include")
target_link_libraries(lg_ui PUBLIC lg_core hba_visage_ui PRIVATE visage lg_ui_brand lg_warnings)

add_executable(lg-ui-snapshot tools/snapshot.cpp)
hba_build_for_host_only(lg-ui-snapshot)
target_link_libraries(lg-ui-snapshot PRIVATE lg_ui lg_warnings)
