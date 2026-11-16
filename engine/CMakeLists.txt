add_library(tf_engine STATIC
    include/tf/engine.h
    include/tf/nonblocking.h
    include/tf/render.h
    include/tf/voice_pitch.h
    src/control.h
    src/crossover.h
    src/decimator.h
    src/denormals.h
    src/feedback_delay.h
    src/fold.h
    src/harmonic.h
    src/lanes.h
    src/limiter.h
    src/squash.h
    src/svf.h
    src/tract.h
    src/voice.h
    src/engine.cpp
    src/render.cpp)

target_include_directories(tf_engine
    PUBLIC include
    PRIVATE src)
# HIIR is a SYSTEM include so our warning set does not apply to it.
target_include_directories(tf_engine SYSTEM PUBLIC third_party/hiir)
target_compile_features(tf_engine PUBLIC cxx_std_20)
target_link_libraries(tf_engine PUBLIC tf_core PRIVATE tf_warnings)

# The audio path is checked by the compiler: Engine::process and everything it
# calls must be provably non-blocking (no allocation, locks, throws, syscalls).
target_compile_options(tf_engine PRIVATE -Wfunction-effects -Werror=function-effects)

# Internal headers, for the unit tests of the individual DSP pieces.
add_library(tf_engine_internal INTERFACE)
target_include_directories(tf_engine_internal INTERFACE src)
target_link_libraries(tf_engine_internal INTERFACE tf_engine)
