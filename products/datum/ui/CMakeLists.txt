add_embedded_resources(dt_ui_brand "dt_brand.h" "dt::ui::brand"
    "${PROJECT_SOURCE_DIR}/brand/logo/datum-logotype-on-dark.svg;${PROJECT_SOURCE_DIR}/brand/logo/datum-mark-on-dark.svg")

add_library(dt_ui STATIC
    include/dt/ui/editor.h
    include/dt/ui/fake_model.h
    include/dt/ui/model.h
    src/anatomy_panel.cpp
    src/compose_panel.cpp
    src/controls.h
    src/editor.cpp
    src/fake_model.cpp
    src/panel.cpp
    src/panels.h
    src/sculpt_panel.cpp
    src/stage_panel.cpp
    src/theme.h
    src/top_bar.cpp
    src/top_bar.h)

target_include_directories(dt_ui PUBLIC include)
target_compile_features(dt_ui PUBLIC cxx_std_20)
target_link_libraries(dt_ui PUBLIC dt_core dt_engine hba_visage_ui PRIVATE visage dt_ui_brand dt_warnings)

add_executable(dt-ui-snapshot tools/snapshot.cpp)
hba_build_for_host_only(dt-ui-snapshot)
target_link_libraries(dt-ui-snapshot PRIVATE dt_ui dt_warnings)
