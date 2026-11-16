add_executable(tf_engine_tests
    helpers.h
    helpers.cpp
    DspTests.cpp
    EngineTests.cpp
    BenchmarkTests.cpp
    HarmonicTests.cpp
    LaneTests.cpp
    LevelTests.cpp
    PresetTests.cpp)
target_link_libraries(tf_engine_tests PRIVATE tf_engine_internal tf_engine tf_core tf_analysis Catch2::Catch2WithMain
                      tf_warnings)
target_compile_definitions(tf_engine_tests PRIVATE TF_PRESETS_DIR="${PROJECT_SOURCE_DIR}/presets")
catch_discover_tests(tf_engine_tests DISCOVERY_MODE PRE_TEST)
