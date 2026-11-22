add_embedded_resources(cl_ui_fonts "cl_fonts.h" "cl::ui::fonts"
    "${CMAKE_CURRENT_SOURCE_DIR}/fonts/Inter-Regular.ttf;${CMAKE_CURRENT_SOURCE_DIR}/fonts/Inter-Medium.ttf;${CMAKE_CURRENT_SOURCE_DIR}/fonts/Inter-SemiBold.ttf")

add_embedded_resources(cl_ui_brand "cl_brand.h" "cl::ui::brand"
    "${PROJECT_SOURCE_DIR}/brand/logo/cutline-logotype-on-dark.svg;${PROJECT_SOURCE_DIR}/brand/logo/cutline-mark-on-dark.svg")

add_library(cl_ui STATIC
    include/cl/ui/editor.h
    include/cl/ui/fake_model.h
    include/cl/ui/model.h
    src/controls.cpp
    src/controls.h
    src/delivery_panel.cpp
    src/editor.cpp
    src/fake_model.cpp
    src/movement_edit.cpp
    src/movement_edit.h
    src/movement_panel.cpp
    src/panel.cpp
    src/panels.h
    src/phrase_panel.cpp
    src/theme.h
    src/top_bar.cpp
    src/top_bar.h)
target_include_directories(cl_ui PUBLIC include)
target_compile_features(cl_ui PUBLIC cxx_std_20)
target_link_libraries(cl_ui PUBLIC cl_core cl_engine PRIVATE visage cl_ui_fonts cl_ui_brand cl_warnings)

add_executable(cl-ui-snapshot tools/snapshot.cpp)
target_link_libraries(cl-ui-snapshot PRIVATE cl_ui cl_warnings)
