add_executable(pv_plugin_tests
    PluginTests.cpp)

target_compile_features(pv_plugin_tests PRIVATE cxx_std_20)
target_link_libraries(pv_plugin_tests PRIVATE PrismVein Catch2::Catch2WithMain pv_warnings)
catch_discover_tests(pv_plugin_tests)
