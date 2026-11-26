add_embedded_resources(tg_ui_brand "tg_brand.h" "tg::ui::brand"
    "${PROJECT_SOURCE_DIR}/brand/logo/torus-glide-logotype-on-dark.svg;${PROJECT_SOURCE_DIR}/brand/logo/torus-glide-mark-on-dark.svg")

add_library(tg_ui STATIC
    include/tg/ui/editor.h
    include/tg/ui/model.h
    src/controls.cpp
    src/controls.h
    src/controls_strip.cpp
    src/editor.cpp
    src/manifold_art.cpp
    src/manifold_art.h
    src/manifold_ribbon.cpp
    src/manifold_ribbon.h
    src/movement_panel.cpp
    src/panels.h
    src/play_panel.cpp
    src/play_panel.h
    src/stage_panel.cpp
    src/theme.h
    src/top_bar.cpp
    src/top_bar.h)

target_include_directories(tg_ui PUBLIC include)
target_compile_features(tg_ui PUBLIC cxx_std_20)
target_link_libraries(tg_ui PUBLIC tg_core tg_engine hba_visage_ui PRIVATE visage tg_ui_brand tg_warnings)

add_executable(tg-ui-snapshot tools/snapshot.cpp)
hba_build_for_host_only(tg-ui-snapshot)
target_link_libraries(tg-ui-snapshot PRIVATE tg_ui tg_warnings)
