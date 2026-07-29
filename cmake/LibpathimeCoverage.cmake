# Test-coverage instrumentation, and the report target that drives it.
#
# **This file is about *test* coverage** — which lines of libpathime the suites
# under `tests/` actually execute. It is unrelated to `LIBPATHIME_TABLE_COVERAGE`
# and the `pathime-table-coverage` target, which are about *glyph* coverage:
# which characters a font can render. The word is overloaded in this tree, so
# every name here carries `TEST_COVERAGE` / `test-coverage` and never the bare
# word alone. See BUILD.md, "Test coverage" and "Glyph coverage" — two sections,
# two subjects.
#
# Included from the top-level CMakeLists immediately after LibpathimeOptions,
# because instrumentation goes on with add_compile_options() and has to be in
# place before the first target is defined.

if(NOT LIBPATHIME_TEST_COVERAGE)
  return()
endif()

# --- What the option requires to mean anything. ---
#
# Coverage without the suites would instrument a tree and then measure nothing,
# so this is an error rather than a quiet implication: turning the tests on
# behind the user's back would make `LIBPATHIME_TEST_COVERAGE=ON` change what
# gets built in a way the option does not name.
if(NOT LIBPATHIME_BUILD_TESTS)
  message(FATAL_ERROR
    "libpathime: LIBPATHIME_TEST_COVERAGE needs LIBPATHIME_BUILD_TESTS=ON — "
    "there is nothing to measure otherwise.")
endif()

# --- Toolchain gate. ---
#
# The instrumentation here is gcov's: `--coverage`, counters written to .gcda
# beside the objects, read back by gcovr. GCC has it and Clang emulates it. MSVC
# has no equivalent at all, and clang-cl does not take `--coverage` — its
# coverage is the LLVM source-based kind (`-fprofile-instr-generate
# -fcoverage-mapping`, then llvm-profdata + llvm-cov), which is a different
# pipeline rather than a different flag spelling. Neither is wired up here;
# BUILD.md, "Test coverage on Windows", carries the manual recipe for both.
set(_lpi_cov_ok FALSE)
if(CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$"
   AND NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  set(_lpi_cov_ok TRUE)
endif()

if(NOT _lpi_cov_ok)
  message(FATAL_ERROR
    "libpathime: LIBPATHIME_TEST_COVERAGE needs a gcov-style toolchain (GCC, or "
    "Clang with the GNU driver); this build uses '${CMAKE_CXX_COMPILER_ID}' with "
    "the '${CMAKE_CXX_COMPILER_FRONTEND_VARIANT}' driver.\n"
    "On Windows use OpenCppCoverage against the MSVC build, or clang-cl's "
    "source-based coverage against the windows-ninja preset. BUILD.md, "
    "\"Test coverage on Windows\", has both recipes.")
endif()

# --- The instrumentation itself. ---
#
# Applied to the whole tree rather than to the libpathime target alone, for the
# same reason the sanitizers are (docs/testing.md): a partially instrumented
# build is the one arrangement that silently reports the wrong thing, and the
# report's own filter is what narrows the result back to src/. The cost is build
# time in the vendored libraries, which no one waits on twice.
#
# These land after CMAKE_<LANG>_FLAGS_<CONFIG>, so `-O0` wins over whatever the
# build type asked for. That is deliberate: with optimisation on, gcov's line
# counts describe the inlined-and-reordered code rather than the code as
# written, and the report becomes a puzzle instead of a measurement.
add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:--coverage>
                    $<$<COMPILE_LANGUAGE:C,CXX>:-O0>
                    $<$<COMPILE_LANGUAGE:C,CXX>:-g>)
add_link_options(--coverage)

# Link-time optimisation and coverage disagree about which code exists.
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)

# --- The tools that read the counters back. ---
#
# gcovr is the one hard requirement; it drives gcov itself and writes all three
# report formats. Missing, it is a configure-time warning rather than an error,
# so that a build which only wants the instrumentation still configures — the
# target then fails with the same message if anyone runs it.
find_program(LIBPATHIME_GCOVR_EXECUTABLE NAMES gcovr
  DOC "gcovr, which turns the .gcda counters into a coverage report")

# gcov must match the compiler that wrote the counters. GCC's own gcov is
# usually versioned alongside it, and a mismatched pair reports a format error
# rather than wrong numbers, so prefer the versioned name. Clang ships the
# reader as a subcommand instead of a program.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  string(REGEX MATCH "^[0-9]+" _lpi_gcc_major "${CMAKE_CXX_COMPILER_VERSION}")
  find_program(LIBPATHIME_GCOV_EXECUTABLE
    NAMES "gcov-${_lpi_gcc_major}" gcov
    DOC "The gcov matching the compiler that wrote the .gcda counters")
else()
  find_program(_lpi_llvm_cov NAMES llvm-cov)
  if(_lpi_llvm_cov)
    set(LIBPATHIME_GCOV_EXECUTABLE "${_lpi_llvm_cov} gcov" CACHE STRING
      "The gcov matching the compiler that wrote the .gcda counters")
  endif()
endif()

if(NOT LIBPATHIME_GCOVR_EXECUTABLE)
  message(WARNING
    "libpathime: LIBPATHIME_TEST_COVERAGE is ON but gcovr was not found. The "
    "build is instrumented, but the pathime-test-coverage target cannot write "
    "a report until gcovr is installed:\n"
    "  pip install gcovr        (or: apt-get install gcovr)")
endif()

# --- The report target. ---
#
# One target does the whole cycle — clear counters, run the suites, write the
# report — because the three steps are only correct together. Counters
# accumulate across runs, so a report taken without clearing first would fold in
# every earlier run *and* the build-time codegen tools, which compile several of
# the engine's own sources (tools/table-compile) and execute them while the
# build runs. Those runs are real executions of src/engines/table/, and counting
# them would credit the test suites with lines only the build ever reached.
#
# The work is in a -P script rather than a chain of COMMANDs so that a failing
# suite still produces a report: a test failure is exactly when the numbers are
# worth reading, and add_custom_target stops at the first non-zero exit.
set(LIBPATHIME_TEST_COVERAGE_OUTPUT_DIR "${PROJECT_BINARY_DIR}/coverage"
  CACHE PATH "Where pathime-test-coverage writes its report")

add_custom_target(pathime-test-coverage
  COMMAND "${CMAKE_COMMAND}"
    "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}"
    "-DBINARY_DIR=${PROJECT_BINARY_DIR}"
    "-DOUTPUT_DIR=${LIBPATHIME_TEST_COVERAGE_OUTPUT_DIR}"
    "-DCTEST_EXECUTABLE=${CMAKE_CTEST_COMMAND}"
    "-DGCOVR_EXECUTABLE=${LIBPATHIME_GCOVR_EXECUTABLE}"
    "-DGCOV_EXECUTABLE=${LIBPATHIME_GCOV_EXECUTABLE}"
    "-DCONFIG=$<CONFIG>"
    -P "${PROJECT_SOURCE_DIR}/cmake/CoverageReport.cmake"
  VERBATIM
  USES_TERMINAL                      # the suites print as they go
  COMMENT "libpathime: measuring test coverage of src/")

# The target deliberately depends on nothing. It runs the suites; it does not
# build them, and a dependency on some of them would be worse than none —
# `--target pathime-test-coverage` would then build a subset and ctest would
# report the rest as "unable to find executable", which reads like a broken
# suite rather than a build that has not happened yet. Build the tree, then ask
# for the report; BUILD.md gives the two lines in that order, and the script
# says so as well when it finds no test executables.

