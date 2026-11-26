add_executable(ln_core_tests core/CoreTests.cpp)
target_link_libraries(ln_core_tests PRIVATE ln_core Catch2::Catch2WithMain ln_warnings)
catch_discover_tests(ln_core_tests)

add_executable(ln_engine_tests engine/EngineTests.cpp)
target_link_libraries(ln_engine_tests PRIVATE ln_engine ln_core Catch2::Catch2WithMain ln_warnings)
catch_discover_tests(ln_engine_tests)

if(LN_BUILD_PLUGIN)
    add_executable(ln_plugin_tests plugin/PluginTests.cpp)
    target_link_libraries(ln_plugin_tests PRIVATE Lineage Catch2::Catch2WithMain ln_warnings)
    catch_discover_tests(ln_plugin_tests)
endif()
