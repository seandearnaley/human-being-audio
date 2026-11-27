add_executable(pv-render pv_render.cpp)
target_compile_features(pv-render PRIVATE cxx_std_20)
target_link_libraries(pv-render PRIVATE pv_engine hba_core pv_warnings)
