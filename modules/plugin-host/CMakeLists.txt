if(NOT TARGET hba_plugin_host_headers)
    add_library(hba_plugin_host_headers INTERFACE)
    target_include_directories(hba_plugin_host_headers INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_compile_features(hba_plugin_host_headers INTERFACE cxx_std_20)
endif()

# INTERFACE sources (linked PRIVATE by each plugin's SharedCode static archive)
# so JUCE modules are compiled exactly once per product rather than duplicated.
if(NOT TARGET hba_plugin_host)
    add_library(hba_plugin_host INTERFACE)
    target_sources(hba_plugin_host INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/include/hba/host/curve_store.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/hba/host/external_file_drag.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/hba/host/midi_expression.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/hba/host/offline_export.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/hba/host/parameters.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/hba/host/plugin_editor.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/hba/host/preset_bank.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/hba/host/sidechain.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/hba/host/state_mailbox.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/hba/host/stereo_analysis.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/hba/host/telemetry_channel.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/hba/host/visage_surface.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/midi_expression.cpp")
    if(APPLE)
        target_sources(hba_plugin_host INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src/MacSurface.mm")
    endif()
    target_link_libraries(hba_plugin_host INTERFACE hba_plugin_host_headers)
endif()
