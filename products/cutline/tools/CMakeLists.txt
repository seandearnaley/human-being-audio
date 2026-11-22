add_executable(cutline-render cutline_render.cpp)
target_link_libraries(cutline-render PRIVATE cl_engine cl_warnings)
