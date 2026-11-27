add_executable(pv_plugin_tests
    PluginTests.cpp)

if(WIN32)
    target_sources(pv_plugin_tests PRIVATE
        "${PROJECT_SOURCE_DIR}/../../modules/plugin-host/tests/WindowsEditorSizeTests.cpp")
endif()

hba_build_for_host_only(pv_plugin_tests)
target_compile_features(pv_plugin_tests PRIVATE cxx_std_20)
target_link_libraries(pv_plugin_tests PRIVATE PrismVein Catch2::Catch2WithMain pv_warnings)
catch_discover_tests(pv_plugin_tests DISCOVERY_MODE PRE_TEST)
