add_embedded_resources(pf_ui_brand "pf_brand.h" "pf::ui::brand"
    "${PROJECT_SOURCE_DIR}/brand/logo/pole-field-logotype-on-dark.svg;${PROJECT_SOURCE_DIR}/brand/logo/pole-field-mark-on-dark.svg")

add_library(pf_ui STATIC
    include/pf/ui/editor.h
    include/pf/ui/fake_model.h
    include/pf/ui/model.h
    src/controls.h
    src/editor.cpp
    src/fake_model.cpp
    src/movement_edit.cpp
    src/movement_edit.h
    src/movement_panel.cpp
    src/output_panel.cpp
    src/panel.cpp
    src/panels.h
    src/theme.h
    src/top_bar.cpp
    src/top_bar.h
    src/zplane_panel.cpp)
target_include_directories(pf_ui PUBLIC include)
target_compile_features(pf_ui PUBLIC cxx_std_20)
target_link_libraries(pf_ui PUBLIC pf_core hba_visage_ui PRIVATE visage pf_ui_brand pf_warnings)

add_executable(pf-ui-snapshot tools/snapshot.cpp)
hba_build_for_host_only(pf-ui-snapshot)
target_link_libraries(pf-ui-snapshot PRIVATE pf_ui pf_warnings)
