# Included once per top-level product by the studio presets.
get_filename_component(hba_product_slug "${CMAKE_SOURCE_DIR}" NAME)
set(hba_quality_script "${CMAKE_CURRENT_LIST_DIR}/../factory/quality/native.py")

# Resolve developer-only tools on demand. Normal configure, build and release need none of them.
add_custom_target(studio-lint
    COMMAND "${CMAKE_COMMAND}" "-DPRODUCT=${hba_product_slug}" "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
        "-DSCRIPT=${hba_quality_script}" -P "${CMAKE_CURRENT_LIST_DIR}/RunTidy.cmake"
    USES_TERMINAL VERBATIM)

function(hba_realtime_suite target filter)
    add_custom_target(check-realtime
        COMMAND "$<TARGET_FILE:${target}>" "${filter}"
        DEPENDS ${target} USES_TERMINAL VERBATIM)
endfunction()
