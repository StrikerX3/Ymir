# Expected inputs:
# - PGO_DIR: directory containing *.profraw
# - PGO_PROFDATA: output .profdata path
# - LLVM_PROFDATA_EXE: path to llvm-profdata
if (NOT DEFINED PGO_DIR OR NOT DEFINED PGO_PROFDATA OR NOT DEFINED LLVM_PROFDATA_EXE)
    message(FATAL_ERROR "PGOMerge.cmake requires PGO_DIR, PGO_PROFDATA, and LLVM_PROFDATA_EXE.")
endif ()

# Sanitize quoted arguments (may be passed by the generator on some systems).
string(REPLACE "\"" "" PGO_DIR "${PGO_DIR}")
string(REPLACE "\"" "" PGO_PROFDATA "${PGO_PROFDATA}")
string(REPLACE "\"" "" LLVM_PROFDATA_EXE "${LLVM_PROFDATA_EXE}")

# Collect profile data and merge.
file(MAKE_DIRECTORY "${PGO_DIR}")
file(GLOB _ymir_profraw_files "${PGO_DIR}/*.profraw")

if (NOT _ymir_profraw_files)
    message(FATAL_ERROR "No .profraw files found in ${PGO_DIR}. Make sure LLVM_PROFILE_FILE points to this directory.")
endif ()

execute_process(
    COMMAND "${LLVM_PROFDATA_EXE}" merge -o "${PGO_PROFDATA}" ${_ymir_profraw_files}
    RESULT_VARIABLE _ymir_merge_result
)

if (NOT _ymir_merge_result EQUAL 0)
    message(FATAL_ERROR "llvm-profdata merge failed with exit code ${_ymir_merge_result}.")
endif ()
