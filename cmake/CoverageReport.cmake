# The body of the `pathime-test-coverage` target — clear counters, run the
# suites, write the report. Run with `cmake -P`, never included.
#
# This is *test* coverage. Glyph coverage is a different subject entirely, in
# tools/generate-coverage.py and the pathime-table-coverage target; see the
# header of LibpathimeCoverage.cmake, which is what invokes this file and where
# every variable below is set.
#
# Expected on the command line:
#   SOURCE_DIR BINARY_DIR OUTPUT_DIR CTEST_EXECUTABLE GCOVR_EXECUTABLE
#   GCOV_EXECUTABLE CONFIG

cmake_minimum_required(VERSION 3.21)

foreach(_required SOURCE_DIR BINARY_DIR OUTPUT_DIR CTEST_EXECUTABLE)
  if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
    message(FATAL_ERROR "CoverageReport.cmake: ${_required} was not set. This "
                        "script is driven by the pathime-test-coverage target, "
                        "not run by hand.")
  endif()
endforeach()

if(NOT GCOVR_EXECUTABLE)
  message(FATAL_ERROR
    "libpathime: gcovr was not found at configure time, so no report can be "
    "written. Install it and re-run CMake:\n"
    "  pip install gcovr        (or: apt-get install gcovr)")
endif()

# --- 1. Clear the counters. -------------------------------------------------
#
# gcov *accumulates*: a .gcda that survives from an earlier run is added to,
# not replaced. Two things would otherwise leak into the numbers — the previous
# report's run, and the build-time codegen tools, which compile several of the
# table engine's own sources into tools/table-compile and then execute them to
# compile the shipped tables. Those executions are real, but they are the
# build's, not the suites'; counting them credits tests with lines only the
# build ever reached.
file(GLOB_RECURSE _gcda "${BINARY_DIR}/*.gcda")
list(LENGTH _gcda _gcda_count)
if(_gcda_count GREATER 0)
  file(REMOVE ${_gcda})
endif()
message(STATUS "libpathime: cleared ${_gcda_count} stale counter file(s)")

# --- 2. Run the suites. -----------------------------------------------------
set(_ctest_args --test-dir "${BINARY_DIR}" --output-on-failure)
if(CONFIG)
  list(APPEND _ctest_args -C "${CONFIG}")   # multi-config generators
endif()

# Serially, on purpose. `ctest -j` has two separate quarrels with this target:
# concurrent processes appending to one .gcda, and the pyzy user database
# several suites share (docs/testing.md). Neither is worth a faster report.
execute_process(
  COMMAND "${CTEST_EXECUTABLE}" ${_ctest_args}
  RESULT_VARIABLE _ctest_rc)

if(NOT _ctest_rc EQUAL 0)
  # Not fatal: a failing suite is precisely when the coverage numbers are worth
  # reading. Say it loudly, report anyway, and fail at the end so that CI still
  # sees a red build.
  message(WARNING
    "libpathime: ctest exited ${_ctest_rc} — the report below covers a run with "
    "failures in it.")
endif()

# --- 3. Write the report. ---------------------------------------------------
#
# `--filter src/` is relative and the working directory is the source tree, both
# deliberately: an absolute filter under a path that is a symlink, a bind mount
# or a Windows drive mapping silently matches nothing and gcovr reports "all
# coverage data is filtered out" — an empty report that looks like a
# configuration problem rather than a path problem.
#
# The filter is what keeps the vendored libraries out. The whole tree is
# instrumented (LibpathimeCoverage.cmake says why), so without it the report
# would be four upstream projects wearing libpathime's name.
file(MAKE_DIRECTORY "${OUTPUT_DIR}")

set(_gcovr_args
  --root "${SOURCE_DIR}"
  --object-directory "${BINARY_DIR}"
  --filter src/
  --exclude-unreachable-branches
  --print-summary
  --txt "${OUTPUT_DIR}/coverage.txt"
  --html-details "${OUTPUT_DIR}/index.html"
  --cobertura "${OUTPUT_DIR}/coverage.xml")

if(GCOV_EXECUTABLE)
  list(APPEND _gcovr_args --gcov-executable "${GCOV_EXECUTABLE}")
endif()

execute_process(
  COMMAND "${GCOVR_EXECUTABLE}" ${_gcovr_args}
  WORKING_DIRECTORY "${SOURCE_DIR}"
  RESULT_VARIABLE _gcovr_rc)

if(NOT _gcovr_rc EQUAL 0)
  message(FATAL_ERROR "libpathime: gcovr exited ${_gcovr_rc}; no report written.")
endif()

message(STATUS "libpathime: coverage report in ${OUTPUT_DIR}")
message(STATUS "  ${OUTPUT_DIR}/index.html    annotated source, per file")
message(STATUS "  ${OUTPUT_DIR}/coverage.txt  the same summary as above")
message(STATUS "  ${OUTPUT_DIR}/coverage.xml  Cobertura, for CI")

# Carry the suites' verdict out to the caller, after the report exists.
if(NOT _ctest_rc EQUAL 0)
  message(FATAL_ERROR "libpathime: the report is written, but ctest failed "
                      "(exit ${_ctest_rc}).")
endif()
