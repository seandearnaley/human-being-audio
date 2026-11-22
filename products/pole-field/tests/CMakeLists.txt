add_executable(pf-core-tests core/CoreTests.cpp)
target_link_libraries(pf-core-tests PRIVATE pf_core Catch2::Catch2WithMain pf_warnings)
catch_discover_tests(pf-core-tests DISCOVERY_MODE PRE_TEST)

add_executable(pf-engine-tests
    engine/EngineTests.cpp
    "${PROJECT_SOURCE_DIR}/../../modules/dsp/tests/GainTests.cpp"
    "${PROJECT_SOURCE_DIR}/../../modules/dsp/tests/AdaaTests.cpp")
target_link_libraries(pf-engine-tests PRIVATE pf_engine_internal Catch2::Catch2WithMain pf_warnings)
catch_discover_tests(pf-engine-tests DISCOVERY_MODE PRE_TEST)
if(HBA_RTSAN)
    hba_realtime_suite(pf-engine-tests "[engine]")
endif()

if(PF_BUILD_PLUGIN)
    add_executable(pf-plugin-tests plugin/PluginTests.cpp)
    if(APPLE)
        target_sources(pf-plugin-tests PRIVATE plugin/MacAboutTests.mm)
    endif()
    target_compile_definitions(pf-plugin-tests PRIVATE $<TARGET_PROPERTY:PoleField,COMPILE_DEFINITIONS> JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(pf-plugin-tests PRIVATE PoleFieldCode Catch2::Catch2WithMain pf_warnings)
    catch_discover_tests(pf-plugin-tests DISCOVERY_MODE PRE_TEST)

    add_executable(pf-ui-tests ui/CurveEditTests.cpp)
    target_include_directories(pf-ui-tests PRIVATE ${PROJECT_SOURCE_DIR}/ui/src)
    target_link_libraries(pf-ui-tests PRIVATE pf_ui Catch2::Catch2WithMain pf_warnings)
    catch_discover_tests(pf-ui-tests DISCOVERY_MODE PRE_TEST)
endif()

if(PF_BUILD_PLUGIN AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(NAME pole-editor-renders
        COMMAND /usr/bin/python3 "${PROJECT_SOURCE_DIR}/../../factory/release/standalone-linux.py"
                "$<TARGET_FILE:PoleField_Standalone>")
endif()
