add_executable(tf_core_tests
    core/CurveTests.cpp
    core/ParamsTests.cpp
    core/PresetTests.cpp)
hba_build_for_host_only(tf_core_tests)
target_link_libraries(tf_core_tests PRIVATE tf_core Catch2::Catch2WithMain tf_warnings)
catch_discover_tests(tf_core_tests DISCOVERY_MODE PRE_TEST)

add_subdirectory(engine)

if(TF_BUILD_PLUGIN)
    add_executable(tf_plugin_tests plugin/PluginTests.cpp)
    hba_build_for_host_only(tf_plugin_tests)
    if(APPLE)
        target_sources(tf_plugin_tests PRIVATE plugin/MacAboutTests.mm)
    endif()
    target_link_libraries(tf_plugin_tests PRIVATE TractForm Catch2::Catch2WithMain tf_warnings)
    catch_discover_tests(tf_plugin_tests DISCOVERY_MODE PRE_TEST)

    # The interface's editing logic, which needs no window. It is internal to tf_ui.
    add_executable(tf_ui_tests ui/CurveEditTests.cpp)
    hba_build_for_host_only(tf_ui_tests)
    target_include_directories(tf_ui_tests PRIVATE ${PROJECT_SOURCE_DIR}/ui/src)
    target_link_libraries(tf_ui_tests PRIVATE tf_ui Catch2::Catch2WithMain tf_warnings)
    catch_discover_tests(tf_ui_tests DISCOVERY_MODE PRE_TEST)
endif()

if(TF_BUILD_PLUGIN AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(NAME tract-editor-renders
        COMMAND /usr/bin/python3 "${PROJECT_SOURCE_DIR}/../../factory/release/standalone-linux.py"
                "$<TARGET_FILE:TractForm_Standalone>")
endif()
