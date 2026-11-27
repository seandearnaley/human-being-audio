add_embedded_resources(pv_ui_brand "pv_brand.h" "pv::ui::brand"
    "${PROJECT_SOURCE_DIR}/brand/logo/prism-vein-logotype-on-dark.svg;${PROJECT_SOURCE_DIR}/brand/logo/prism-vein-mark-on-dark.svg")

add_library(pv_ui STATIC
    include/pv/ui/editor.h
    include/pv/ui/fake_model.h
    include/pv/ui/model.h
    src/color_panel.cpp
    src/controls.h
    src/editor.cpp
    src/fake_model.cpp
    src/lattice_panel.cpp
    src/movement_panel.cpp
    src/panel.cpp
    src/panels.h
    src/stage_panel.cpp
    src/theme.h
    src/top_bar.cpp
    src/top_bar.h)

target_include_directories(pv_ui PUBLIC include)
target_compile_features(pv_ui PUBLIC cxx_std_20)
target_link_libraries(pv_ui PUBLIC pv_core pv_engine hba_visage_ui PRIVATE visage pv_ui_brand pv_warnings)

add_executable(pv-ui-snapshot tools/snapshot.cpp)
hba_build_for_host_only(pv-ui-snapshot)
target_link_libraries(pv-ui-snapshot PRIVATE pv_ui pv_warnings)
