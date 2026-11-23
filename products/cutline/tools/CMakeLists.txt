add_executable(cutline-render cutline_render.cpp)
hba_build_for_host_only(cutline-render)
target_link_libraries(cutline-render PRIVATE cl_engine cl_warnings)
