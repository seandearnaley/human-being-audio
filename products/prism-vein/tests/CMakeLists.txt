if(PV_BUILD_TESTS)
    add_executable(pv_core_tests CoreTests.cpp)
    hba_build_for_host_only(pv_core_tests)
    target_compile_features(pv_core_tests PRIVATE cxx_std_20)
    target_link_libraries(pv_core_tests PRIVATE pv_core Catch2::Catch2WithMain pv_warnings)
    catch_discover_tests(pv_core_tests DISCOVERY_MODE PRE_TEST)

    add_executable(pv_engine_tests EngineTests.cpp)
    hba_build_for_host_only(pv_engine_tests)
    target_compile_features(pv_engine_tests PRIVATE cxx_std_20)
    target_link_libraries(pv_engine_tests PRIVATE pv_engine pv_core Catch2::Catch2WithMain pv_warnings)
    if(HBA_RTSAN)
        hba_realtime_suite(pv_engine_tests "[engine]")
    else()
        catch_discover_tests(pv_engine_tests DISCOVERY_MODE PRE_TEST)
    endif()

    if(PV_BUILD_PLUGIN AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/plugin/CMakeLists.txt")
        add_subdirectory(plugin)
    endif()

    if(PV_BUILD_PLUGIN AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_test(NAME prism-vein-editor-renders
            COMMAND /usr/bin/python3 "${PROJECT_SOURCE_DIR}/../../factory/release/standalone-linux.py"
                    "$<TARGET_FILE:PrismVein_Standalone>")
    endif()
endif()
