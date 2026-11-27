add_library(cl_engine STATIC
    include/cl/engine.h
    include/cl/groove_state.h
    include/cl/held_notes.h
    include/cl/hits.h
    include/cl/midi_event.h
    include/cl/phrase.h
    include/cl/resolve.h
    include/cl/smf.h
    src/audition.h
    src/engine.cpp
    src/engine_impl.h
    src/pad_programs.cpp
    src/pad_programs.h
    src/phrase.cpp
    src/resolve.cpp
    src/smf.cpp)

target_include_directories(cl_engine
    PUBLIC include
    PRIVATE src)
target_compile_features(cl_engine PUBLIC cxx_std_20)
target_link_libraries(cl_engine PUBLIC cl_core hba_dsp PRIVATE cl_warnings)
hba_require_nonblocking(cl_engine)

add_library(cl_engine_internal INTERFACE)
target_include_directories(cl_engine_internal INTERFACE src)
target_link_libraries(cl_engine_internal INTERFACE cl_engine)
