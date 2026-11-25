add_embedded_resources(sb_ui_brand "sb_brand.h" "sb::ui::brand"
    "${PROJECT_SOURCE_DIR}/brand/logo/strike-body-logotype-on-dark.svg;${PROJECT_SOURCE_DIR}/brand/logo/strike-body-mark-on-dark.svg")

add_library(sb_ui STATIC
    include/sb/ui/editor.h
    include/sb/ui/model.h
    src/controls.cpp
    src/controls.h
    src/editor.cpp
    src/macro_panel.cpp
    src/panels.h
    src/rack_panel.cpp
    src/stage_panel.cpp
    src/theme.h
    src/top_bar.cpp
    src/top_bar.h)

target_include_directories(sb_ui PUBLIC include)
target_compile_features(sb_ui PUBLIC cxx_std_20)
target_link_libraries(sb_ui PUBLIC sb_core sb_engine hba_visage_ui PRIVATE visage sb_ui_brand sb_warnings)

add_executable(sb-ui-snapshot tools/snapshot.cpp)
hba_build_for_host_only(sb-ui-snapshot)
target_link_libraries(sb-ui-snapshot PRIVATE sb_ui sb_warnings)
