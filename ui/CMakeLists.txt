# Inter, SIL Open Font License. See fonts/LICENSE.txt.
add_embedded_resources(tf_ui_fonts "tf_fonts.h" "tf::ui::fonts"
    "${CMAKE_CURRENT_SOURCE_DIR}/fonts/Inter-Regular.ttf;${CMAKE_CURRENT_SOURCE_DIR}/fonts/Inter-Medium.ttf;${CMAKE_CURRENT_SOURCE_DIR}/fonts/Inter-SemiBold.ttf")

add_library(tf_ui STATIC
    include/tf/ui/editor.h
    src/editor.cpp)
target_include_directories(tf_ui PUBLIC include)
target_compile_features(tf_ui PUBLIC cxx_std_20)
target_link_libraries(tf_ui PUBLIC tf_core PRIVATE visage tf_ui_fonts tf_warnings)

add_executable(tf-ui-snapshot tools/snapshot.cpp)
target_link_libraries(tf-ui-snapshot PRIVATE tf_ui tf_warnings)
