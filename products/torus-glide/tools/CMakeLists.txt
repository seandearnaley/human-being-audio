add_executable(tg-render render.cpp)
hba_build_for_host_only(tg-render)
target_link_libraries(tg-render PRIVATE tg_engine tg_warnings)
