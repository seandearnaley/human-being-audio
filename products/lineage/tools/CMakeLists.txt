add_executable(ln-render ln_render.cpp)
hba_build_for_host_only(ln-render)
target_link_libraries(ln-render PRIVATE ln_engine ln_core hba_core ln_warnings)
