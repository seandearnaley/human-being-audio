add_executable(sb-render render.cpp)
hba_build_for_host_only(sb-render)
target_link_libraries(sb-render PRIVATE sb_engine sb_warnings)
