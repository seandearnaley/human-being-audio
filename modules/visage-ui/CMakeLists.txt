if(NOT TARGET hba_visage_ui)
    add_embedded_resources(hba_inter_fonts "hba_fonts.h" "hba::ui::fonts"
        "${CMAKE_CURRENT_SOURCE_DIR}/fonts/Inter-Regular.ttf;${CMAKE_CURRENT_SOURCE_DIR}/fonts/Inter-Medium.ttf;${CMAKE_CURRENT_SOURCE_DIR}/fonts/Inter-SemiBold.ttf")

    add_library(hba_visage_ui STATIC
        include/hba/ui/controls.h
        include/hba/ui/editor_window.h
        include/hba/ui/movement_edit.h
        include/hba/ui/movement_panel.h
        include/hba/ui/theme.h
        include/hba/ui/top_bar.h
        include/hba/ui/voice_bar.h
        src/movement_edit.cpp)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_sources(hba_visage_ui PRIVATE src/x11_file_drag.cpp)
        set_source_files_properties(src/x11_file_drag.cpp PROPERTIES COMPILE_DEFINITIONS VISAGE_LINUX=1)
    endif()
    target_include_directories(hba_visage_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_compile_features(hba_visage_ui PUBLIC cxx_std_20)
    if(MSVC)
        target_compile_options(hba_visage_ui PRIVATE /W4)
    else()
        target_compile_options(hba_visage_ui PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    endif()
    target_link_libraries(hba_visage_ui PUBLIC hba_dsp visage hba_inter_fonts)
endif()
