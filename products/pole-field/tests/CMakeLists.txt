add_executable(pf-core-tests core/CoreTests.cpp)
hba_build_for_host_only(pf-core-tests)
target_link_libraries(pf-core-tests PRIVATE pf_core Catch2::Catch2WithMain pf_warnings)
catch_discover_tests(pf-core-tests DISCOVERY_MODE PRE_TEST)

add_executable(pf-engine-tests
    engine/EngineTests.cpp
    "${PROJECT_SOURCE_DIR}/../../modules/dsp/tests/GainTests.cpp"
    "${PROJECT_SOURCE_DIR}/../../modules/dsp/tests/AdaaTests.cpp"
    "${PROJECT_SOURCE_DIR}/../../modules/dsp/tests/CurveTests.cpp"
    "${PROJECT_SOURCE_DIR}/../../modules/dsp/tests/DspModulesTests.cpp")
hba_build_for_host_only(pf-engine-tests)
target_include_directories(pf-engine-tests PRIVATE "${PROJECT_SOURCE_DIR}/../../modules/studio-ui")
target_link_libraries(pf-engine-tests PRIVATE pf_engine_internal Catch2::Catch2WithMain pf_warnings)
if(HBA_RTSAN)
    hba_realtime_suite(pf-engine-tests "[engine]")
else()
    catch_discover_tests(pf-engine-tests DISCOVERY_MODE PRE_TEST)
endif()

if(PF_BUILD_PLUGIN)
    add_executable(pf-plugin-tests plugin/PluginTests.cpp)
    hba_build_for_host_only(pf-plugin-tests)
    if(APPLE)
        target_sources(pf-plugin-tests PRIVATE plugin/MacAboutTests.mm
            "${PROJECT_SOURCE_DIR}/../../modules/plugin-host/tests/MacFileDropTests.mm")
    endif()
    target_link_libraries(pf-plugin-tests PRIVATE PoleField Catch2::Catch2WithMain pf_warnings)
    catch_discover_tests(pf-plugin-tests DISCOVERY_MODE PRE_TEST)

    add_executable(pf-ui-tests ui/CurveEditTests.cpp)
    hba_build_for_host_only(pf-ui-tests)
    target_include_directories(pf-ui-tests PRIVATE ${PROJECT_SOURCE_DIR}/ui/src)
    target_link_libraries(pf-ui-tests PRIVATE pf_ui Catch2::Catch2WithMain pf_warnings)
    catch_discover_tests(pf-ui-tests DISCOVERY_MODE PRE_TEST)
endif()

if(PF_BUILD_PLUGIN AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(NAME pole-editor-renders
        COMMAND /usr/bin/python3 "${PROJECT_SOURCE_DIR}/../../factory/release/standalone-linux.py"
                "$<TARGET_FILE:PoleField_Standalone>")
endif()
