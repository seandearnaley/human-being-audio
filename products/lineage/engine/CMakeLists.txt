add_library(ln_engine STATIC
    include/ln/engine.h
    src/engine.cpp)

target_include_directories(ln_engine PUBLIC include)
target_compile_features(ln_engine PUBLIC cxx_std_20)
target_link_libraries(ln_engine PUBLIC ln_core hba_dsp PRIVATE ln_warnings)
hba_require_nonblocking(ln_engine)
