find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
find_program(TIDY NAMES clang-tidy-22 clang-tidy REQUIRED)
find_program(RUN_TIDY NAMES run-clang-tidy-22 run-clang-tidy run-clang-tidy.py REQUIRED)
execute_process(COMMAND "${TIDY}" --version OUTPUT_VARIABLE version COMMAND_ERROR_IS_FATAL ANY)
if(NOT version MATCHES "version 22\\.")
    message(FATAL_ERROR "Studio lint uses LLVM 22. Found: ${version}")
endif()
execute_process(COMMAND "${Python3_EXECUTABLE}" "${SCRIPT}" lint "${PRODUCT}" "${BUILD_DIR}"
    --runner "${RUN_TIDY}" --tidy "${TIDY}" COMMAND_ERROR_IS_FATAL ANY)
