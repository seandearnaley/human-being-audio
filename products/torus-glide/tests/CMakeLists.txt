add_executable(tg_core_tests
    core/CoreTests.cpp)
hba_build_for_host_only(tg_core_tests)
target_link_libraries(tg_core_tests PRIVATE tg_core Catch2::Catch2WithMain tg_warnings)
catch_discover_tests(tg_core_tests DISCOVERY_MODE PRE_TEST)

add_executable(tg_engine_tests
    engine/EngineTests.cpp)
hba_build_for_host_only(tg_engine_tests)
target_link_libraries(tg_engine_tests PRIVATE tg_engine Catch2::Catch2WithMain tg_warnings)
if(HBA_RTSAN)
    hba_realtime_suite(tg_engine_tests "[engine]")
else()
    catch_discover_tests(tg_engine_tests DISCOVERY_MODE PRE_TEST)
endif()

if(TG_BUILD_PLUGIN)
    add_executable(tg_plugin_tests
        plugin/PluginTests.cpp)
    hba_build_for_host_only(tg_plugin_tests)
    target_link_libraries(tg_plugin_tests PRIVATE TorusGlide Catch2::Catch2WithMain tg_warnings)
    catch_discover_tests(tg_plugin_tests DISCOVERY_MODE PRE_TEST)
endif()

if(TG_BUILD_PLUGIN AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(NAME torus-glide-editor-renders
        COMMAND /usr/bin/python3 "${PROJECT_SOURCE_DIR}/../../factory/release/standalone-linux.py"
                "$<TARGET_FILE:TorusGlide_Standalone>")
endif()
