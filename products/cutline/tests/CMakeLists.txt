add_executable(cl-core-tests core/CoreTests.cpp core/GrooveTests.cpp)
hba_build_for_host_only(cl-core-tests)
target_link_libraries(cl-core-tests PRIVATE cl_core Catch2::Catch2WithMain cl_warnings)
catch_discover_tests(cl-core-tests DISCOVERY_MODE PRE_TEST)

add_executable(cl-engine-tests
    engine/EngineTests.cpp
    engine/ResolveTests.cpp
    "${PROJECT_SOURCE_DIR}/../../modules/dsp/tests/GainTests.cpp"
    "${PROJECT_SOURCE_DIR}/../../modules/dsp/tests/CurveTests.cpp")
hba_build_for_host_only(cl-engine-tests)
target_link_libraries(cl-engine-tests PRIVATE cl_engine_internal Catch2::Catch2WithMain cl_warnings)
if(HBA_RTSAN)
    hba_realtime_suite(cl-engine-tests "[engine]")
else()
    catch_discover_tests(cl-engine-tests DISCOVERY_MODE PRE_TEST)
endif()

if(CL_BUILD_PLUGIN)
    add_executable(cl-plugin-tests plugin/PluginTests.cpp)
    hba_build_for_host_only(cl-plugin-tests)
    if(APPLE)
        target_sources(cl-plugin-tests PRIVATE plugin/MacAboutTests.mm)
    endif()
    target_link_libraries(cl-plugin-tests PRIVATE Cutline Catch2::Catch2WithMain cl_warnings)
    catch_discover_tests(cl-plugin-tests DISCOVERY_MODE PRE_TEST)

    add_executable(cl-ui-tests ui/CurveEditTests.cpp)
    hba_build_for_host_only(cl-ui-tests)
    target_include_directories(cl-ui-tests PRIVATE ${PROJECT_SOURCE_DIR}/ui/src)
    target_link_libraries(cl-ui-tests PRIVATE cl_ui Catch2::Catch2WithMain cl_warnings)
    catch_discover_tests(cl-ui-tests DISCOVERY_MODE PRE_TEST)
endif()

if(CL_BUILD_PLUGIN AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(NAME cutline-editor-renders
        COMMAND /usr/bin/python3 "${PROJECT_SOURCE_DIR}/../../factory/release/standalone-linux.py"
                "$<TARGET_FILE:Cutline_Standalone>")
endif()
