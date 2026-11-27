add_embedded_resources(ln_ui_brand "ln_brand.h" "ln::ui::brand"
    "${PROJECT_SOURCE_DIR}/brand/logo/lineage-logotype-on-dark.svg;${PROJECT_SOURCE_DIR}/brand/logo/lineage-mark-on-dark.svg")

add_library(ln_ui STATIC
    include/ln/ui/editor.h
    include/ln/ui/fake_model.h
    include/ln/ui/model.h
    src/controls.h
    src/editor.cpp
    src/fake_model.cpp
    src/movement_edit.cpp
    src/movement_edit.h
    src/movement_panel.cpp
    src/panel.cpp
    src/panels.h
    src/play_panel.cpp
    src/play_panel.h
    src/strata_art.cpp
    src/strata_art.h
    src/strata_ribbon.cpp
    src/studio_deck.cpp
    src/studio_stage.cpp
    src/theme.h
    src/top_bar.cpp
    src/top_bar.h)

target_include_directories(ln_ui PUBLIC include)
target_compile_features(ln_ui PUBLIC cxx_std_20)
target_link_libraries(ln_ui PUBLIC ln_core ln_engine hba_visage_ui PRIVATE visage ln_ui_brand ln_warnings)

add_executable(ln-ui-snapshot tools/snapshot.cpp)
hba_build_for_host_only(ln-ui-snapshot)
target_link_libraries(ln-ui-snapshot PRIVATE ln_ui ln_warnings)
