if(NOT TARGET hba_dsp)
    add_library(hba_dsp INTERFACE)
    target_include_directories(hba_dsp INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
endif()
