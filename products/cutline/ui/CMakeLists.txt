add_embedded_resources(cl_ui_brand "cl_brand.h" "cl::ui::brand"
    "${PROJECT_SOURCE_DIR}/brand/logo/cutline-logotype-on-dark.svg;${PROJECT_SOURCE_DIR}/brand/logo/cutline-mark-on-dark.svg")

add_library(cl_ui STATIC
    include/cl/ui/editor.h
    include/cl/ui/fake_model.h
    include/cl/ui/model.h
    src/controls.h
    src/delivery_panel.cpp
    src/editor.cpp
    src/fake_model.cpp
    src/grid_edit.cpp
    src/grid_edit.h
    src/grid_stage.h
    src/grid_stage_draw.cpp
    src/grid_stage_input.cpp
    src/movement_edit.cpp
    src/movement_edit.h
    src/movement_panel.cpp
    src/pad_art.cpp
    src/pad_art.h
    src/panel.cpp
    src/panels.h
    src/play_panel.cpp
    src/play_panel.h
    src/pulse_ribbon.cpp
    src/pulse_ribbon.h
    src/setup_panel.cpp
    src/theme.h
    src/top_bar.cpp
    src/top_bar.h)
target_include_directories(cl_ui PUBLIC include)
target_compile_features(cl_ui PUBLIC cxx_std_20)
target_link_libraries(cl_ui PUBLIC cl_core cl_engine hba_visage_ui PRIVATE visage cl_ui_brand cl_warnings)

add_executable(cl-ui-snapshot tools/snapshot.cpp)
hba_build_for_host_only(cl-ui-snapshot)
target_link_libraries(cl-ui-snapshot PRIVATE cl_ui cl_warnings)
