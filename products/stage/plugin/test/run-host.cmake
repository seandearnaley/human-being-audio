# Runs the headless host for one second with three tracks and requires a clean exit and its closing
# line. It needs no hub: with the Stage app closed every track simply stays unconnected.
#   cmake -DHOST=<path to stage-link-host> -P run-host.cmake
execute_process(COMMAND "${HOST}" 1 120 256 tracks
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 30)
message("${output}${errors}")
if(NOT result EQUAL 0)
    message(FATAL_ERROR "stage-link-host exited with ${result}")
endif()
if(NOT output MATCHES "(^|\n)READY\n")
    message(FATAL_ERROR "stage-link-host never printed READY")
endif()
if(NOT output MATCHES "\nDONE dropped=[0-9]+ onHub=[0-9]+\n$")
    message(FATAL_ERROR "stage-link-host never printed its DONE line")
endif()
