add_executable(tf_core_tests
    core/CurveTests.cpp
    core/ParamsTests.cpp
    core/PresetTests.cpp)
target_link_libraries(tf_core_tests PRIVATE tf_core Catch2::Catch2WithMain tf_warnings)
catch_discover_tests(tf_core_tests DISCOVERY_MODE PRE_TEST)

add_subdirectory(engine)

if(TF_BUILD_PLUGIN)
    add_executable(tf_plugin_tests plugin/PluginTests.cpp)
    # The JucePlugin_* definitions only exist on the plugin target, so copy them.
    target_compile_definitions(tf_plugin_tests PRIVATE $<TARGET_PROPERTY:TractForm,COMPILE_DEFINITIONS>)
    target_link_libraries(tf_plugin_tests PRIVATE TractFormCode Catch2::Catch2WithMain tf_warnings)
    catch_discover_tests(tf_plugin_tests DISCOVERY_MODE PRE_TEST)
endif()
