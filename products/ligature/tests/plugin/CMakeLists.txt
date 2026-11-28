add_executable(lg_plugin_tests
    PluginTests.cpp)

if(WIN32)
    target_sources(lg_plugin_tests PRIVATE
        "${PROJECT_SOURCE_DIR}/../../modules/plugin-host/tests/WindowsEditorSizeTests.cpp")
endif()

hba_build_for_host_only(lg_plugin_tests)
target_compile_features(lg_plugin_tests PRIVATE cxx_std_20)
target_link_libraries(lg_plugin_tests PRIVATE Ligature Catch2::Catch2WithMain lg_warnings)
catch_discover_tests(lg_plugin_tests DISCOVERY_MODE PRE_TEST)
