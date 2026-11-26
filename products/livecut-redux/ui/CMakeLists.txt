# The Livecut Redux 2.0 Visage GPU UI library (JUCE-free for fast isolated snapshots and tests).
add_library(lc_ui STATIC
    include/lc/ui/editor.h
    include/lc/ui/fake_model.h
    include/lc/ui/model.h
    src/classic_panel.cpp
    src/classic_panel.h
    src/controls.cpp
    src/controls.h
    src/editor.cpp
    src/fake_model.cpp
    src/pad_art.cpp
    src/pad_art.h
    src/pad_panel.cpp
    src/pad_panel.h
    src/phrase_tape.cpp
    src/phrase_tape.h
    src/theme.h
    src/top_bar.cpp
    src/top_bar.h)

target_include_directories(lc_ui PUBLIC include)
target_compile_features(lc_ui PUBLIC cxx_std_20)
target_link_libraries(lc_ui PUBLIC LivecutEngine hba_visage_ui PRIVATE visage)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(lc_ui PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
elseif(MSVC)
    target_compile_options(lc_ui PRIVATE /W4)
else()
    target_compile_options(lc_ui PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endif()

add_executable(lc-ui-snapshot tools/snapshot.cpp)
hba_build_for_host_only(lc-ui-snapshot)
target_link_libraries(lc-ui-snapshot PRIVATE lc_ui)
