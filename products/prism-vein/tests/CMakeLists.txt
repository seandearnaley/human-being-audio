add_executable(pv_tests
    CoreTests.cpp
    EngineTests.cpp)

target_compile_features(pv_tests PRIVATE cxx_std_20)
target_link_libraries(pv_tests PRIVATE pv_engine Catch2::Catch2WithMain pv_warnings)
catch_discover_tests(pv_tests)

if(PV_BUILD_PLUGIN AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/plugin/CMakeLists.txt")
    add_subdirectory(plugin)
endif()
