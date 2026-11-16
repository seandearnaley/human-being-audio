add_executable(tf_core_tests
    core/ParamsTests.cpp
    core/PresetTests.cpp)
target_link_libraries(tf_core_tests PRIVATE tf_core Catch2::Catch2WithMain tf_warnings)
catch_discover_tests(tf_core_tests DISCOVERY_MODE PRE_TEST)

add_subdirectory(engine)
