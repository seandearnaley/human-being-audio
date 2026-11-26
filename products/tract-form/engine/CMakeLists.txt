add_library(tf_engine STATIC
    include/tf/engine.h
    include/tf/nonblocking.h
    include/tf/render.h
    include/tf/voice_pitch.h
    src/feedback_delay.h
    src/fold.h
    src/harmonic.h
    src/limiter.h
    src/squash.h
    src/tract.h
    src/voice.h
    src/engine_impl.h
    src/engine_control.cpp
    src/engine.cpp
    src/render.cpp)

target_include_directories(tf_engine
    PUBLIC include
    PRIVATE src)
target_compile_features(tf_engine PUBLIC cxx_std_20)
target_link_libraries(tf_engine PUBLIC tf_core hba_dsp PRIVATE tf_warnings)
hba_require_nonblocking(tf_engine)

# Internal headers, for the unit tests of the individual DSP pieces.
add_library(tf_engine_internal INTERFACE)
target_include_directories(tf_engine_internal INTERFACE src)
target_link_libraries(tf_engine_internal INTERFACE tf_engine)
