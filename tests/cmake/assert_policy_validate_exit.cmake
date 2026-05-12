# assert_policy_validate_exit.cmake
# Helper script to execute bytetaper-validate-policy and verify exact exit codes and stderr matching

if(NOT DEFINED VALIDATE_BIN)
  message(FATAL_ERROR "VALIDATE_BIN is not defined")
endif()
if(NOT DEFINED FIXTURE_PATH)
  message(FATAL_ERROR "FIXTURE_PATH is not defined")
endif()
if(NOT DEFINED EXPECTED_EXIT)
  message(FATAL_ERROR "EXPECTED_EXIT is not defined")
endif()

message(STATUS "Running: ${VALIDATE_BIN} ${FIXTURE_PATH}")

execute_process(
  COMMAND "${VALIDATE_BIN}" "${FIXTURE_PATH}"
  RESULT_VARIABLE ACTUAL_EXIT
  ERROR_VARIABLE ACTUAL_STDERR
  OUTPUT_VARIABLE ACTUAL_STDOUT
)

# If ACTUAL_EXIT is empty, it means success (exit code 0) in some CMake versions, so map it to 0.
if(NOT DEFINED ACTUAL_EXIT OR ACTUAL_EXIT STREQUAL "")
  set(ACTUAL_EXIT 0)
endif()

# Check exit code
if(NOT ACTUAL_EXIT EQUAL EXPECTED_EXIT)
  message(FATAL_ERROR "Validation of ${FIXTURE_PATH} failed!\n"
                      "Expected exit code: ${EXPECTED_EXIT}\n"
                      "Actual exit code:   ${ACTUAL_EXIT}\n"
                      "Stderr:\n${ACTUAL_STDERR}\n"
                      "Stdout:\n${ACTUAL_STDOUT}")
endif()

# Check stderr regex if EXPECTED_CONTENT is specified
if(EXPECTED_CONTENT)
  if(NOT ACTUAL_STDERR MATCHES "${EXPECTED_CONTENT}")
    message(FATAL_ERROR "Validation output of ${FIXTURE_PATH} did not contain expected pattern: '${EXPECTED_CONTENT}'\n"
                        "Actual Stderr:\n${ACTUAL_STDERR}")
  endif()
endif()

message(STATUS "Validation passed successfully with expected exit ${EXPECTED_EXIT}")
