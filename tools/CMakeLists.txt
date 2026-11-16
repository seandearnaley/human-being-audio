# Offline analysis shared by tf-render's metrics and the engine tests.
add_library(tf_analysis STATIC analysis/analysis.h analysis/analysis.cpp)
target_include_directories(tf_analysis PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(tf_analysis PUBLIC cxx_std_20)
target_link_libraries(tf_analysis PRIVATE tf_warnings)

add_executable(tf-render tf_render.cpp)
target_link_libraries(tf-render PRIVATE tf_engine tf_core tf_analysis tf_warnings)
