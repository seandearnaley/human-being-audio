if(NOT TARGET hba_dsp)
    add_library(hba_dsp INTERFACE)
    target_include_directories(hba_dsp INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    # HIIR is exposed as a SYSTEM include so strict studio warning flags do not apply to it.
    target_include_directories(hba_dsp SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/hiir")
endif()
