add_executable(cl-core-tests core/CoreTests.cpp core/GrooveTests.cpp core/GrooveJsonTests.cpp core/PresetTests.cpp core/FactoryPresetTests.cpp core/VaryTests.cpp)
hba_build_for_host_only(cl-core-tests)
target_link_libraries(cl-core-tests PRIVATE cl_core Catch2::Catch2WithMain cl_warnings)
target_compile_definitions(cl-core-tests PRIVATE
    CL_PATTERNS_DIR="${PROJECT_SOURCE_DIR}/patterns"
    CL_PRESETS_DIR="${PROJECT_SOURCE_DIR}/presets")
catch_discover_tests(cl-core-tests DISCOVERY_MODE PRE_TEST)

add_executable(cl-engine-tests
    engine/EngineTests.cpp
    engine/PadTests.cpp
    engine/SlicerTests.cpp
    engine/SmfTests.cpp
    engine/ResolveTests.cpp
    engine/HitsTests.cpp
    engine/PatternLintTests.cpp
    "${PROJECT_SOURCE_DIR}/../../modules/dsp/tests/GainTests.cpp"
    "${PROJECT_SOURCE_DIR}/../../modules/dsp/tests/CurveTests.cpp")
hba_build_for_host_only(cl-engine-tests)
target_link_libraries(cl-engine-tests PRIVATE cl_engine_internal Catch2::Catch2WithMain cl_warnings)
target_compile_definitions(cl-engine-tests PRIVATE
    CL_PATTERNS_DIR="${PROJECT_SOURCE_DIR}/patterns"
    CL_FILLS_DIR="${PROJECT_SOURCE_DIR}/fills")
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

    add_executable(cl-ui-tests ui/CurveEditTests.cpp ui/GridEditTests.cpp)
    hba_build_for_host_only(cl-ui-tests)
    target_sources(cl-ui-tests PRIVATE ui/PlaySurfaceTests.cpp)
    if(WIN32)
        target_sources(cl-ui-tests PRIVATE "${PROJECT_SOURCE_DIR}/../../modules/visage-ui/tests/windows/WindowsGlyphLifetimeTests.cpp")
        target_link_libraries(cl-ui-tests PRIVATE visage ole32)
    endif()
    target_include_directories(cl-ui-tests PRIVATE ${PROJECT_SOURCE_DIR}/ui/src)
    target_link_libraries(cl-ui-tests PRIVATE cl_ui Catch2::Catch2WithMain cl_warnings)
    catch_discover_tests(cl-ui-tests DISCOVERY_MODE PRE_TEST)
endif()

if(CL_BUILD_PLUGIN AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(NAME cutline-editor-renders
        COMMAND /usr/bin/python3 "${PROJECT_SOURCE_DIR}/../../factory/release/standalone-linux.py"
                "$<TARGET_FILE:Cutline_Standalone>")
endif()
