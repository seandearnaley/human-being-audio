add_executable(dt-render dt_render.cpp)
hba_build_for_host_only(dt-render)
target_link_libraries(dt-render PRIVATE dt_engine dt_core dt_warnings)
