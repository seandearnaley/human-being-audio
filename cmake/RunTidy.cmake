find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
set(_hba_llvm_hints
    /opt/homebrew/opt/llvm@22/bin
    /usr/local/opt/llvm@22/bin
    /opt/homebrew/opt/llvm/bin
    /usr/local/opt/llvm/bin)
set(_hba_clang_share_hints
    /opt/homebrew/opt/llvm@22/share/clang
    /usr/local/opt/llvm@22/share/clang
    /opt/homebrew/opt/llvm/share/clang
    /usr/local/opt/llvm/share/clang)
find_program(TIDY NAMES clang-tidy-22 clang-tidy HINTS ${_hba_llvm_hints} REQUIRED)
find_program(RUN_TIDY NAMES run-clang-tidy-22 run-clang-tidy run-clang-tidy.py
    HINTS ${_hba_llvm_hints} ${_hba_clang_share_hints} REQUIRED)
execute_process(COMMAND "${TIDY}" --version OUTPUT_VARIABLE version COMMAND_ERROR_IS_FATAL ANY)
if(NOT version MATCHES "version 22\\.")
    message(FATAL_ERROR "Studio lint uses LLVM 22. Found: ${version}")
endif()
execute_process(COMMAND "${Python3_EXECUTABLE}" "${SCRIPT}" lint "${PRODUCT}" "${BUILD_DIR}"
    --runner "${RUN_TIDY}" --tidy "${TIDY}" COMMAND_ERROR_IS_FATAL ANY)
