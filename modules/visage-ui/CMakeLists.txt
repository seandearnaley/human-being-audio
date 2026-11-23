if(NOT TARGET hba_visage_ui)
    add_embedded_resources(hba_inter_fonts "hba_fonts.h" "hba::ui::fonts"
        "${CMAKE_CURRENT_SOURCE_DIR}/fonts/Inter-Regular.ttf;${CMAKE_CURRENT_SOURCE_DIR}/fonts/Inter-Medium.ttf;${CMAKE_CURRENT_SOURCE_DIR}/fonts/Inter-SemiBold.ttf")

    add_library(hba_visage_ui STATIC
        include/hba/ui/movement_edit.h
        include/hba/ui/theme.h
        src/movement_edit.cpp)
    target_include_directories(hba_visage_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_compile_features(hba_visage_ui PUBLIC cxx_std_20)
    target_compile_options(hba_visage_ui PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    target_link_libraries(hba_visage_ui PUBLIC hba_dsp visage hba_inter_fonts)
endif()
