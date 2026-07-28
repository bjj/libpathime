# anthy-unicode tests.
#
# Two groups, both registered under the `anthy.` prefix:
#
#   anthy.vendor.*  upstream's own test/ programs, wired up the way
#                   anthy-unicode/test/meson.build wires them.
#   anthy.*         tests written here, whose job is to show that the Windows
#                   port behaves the same as the Linux one. They are plain C11
#                   and are meant to run on both.
#
# The awkward part is pointing the programs at the dictionary this build just
# produced rather than at an installed anthy. anthy finds its data through the
# conf database in src-diclib/conf.c: anthy_conf_override() sets a value, and
# anything not set falls back to getenv() of the same name. Upstream's tests
# hard-code build-tree-relative paths ("../anthy-unicode.conf",
# "../mkanthydic/anthy.dic") into those overrides, so they can only be steered
# by choosing their working directory — which is why the port lays its
# dictionary tree out exactly like meson's and publishes ANTHY_TEST_RUNDIR. Our
# own tests take absolute paths as -D defines instead.

if(NOT TARGET anthy-dic)
  message(FATAL_ERROR
    "anthy tests need the dictionary target from cmake/ports/anthy-unicode.")
endif()

set(_vendor_test "${ANTHY_TEST_SRC_DIR}/test")

# Per-test scratch HOME. anthy writes its record file and personal dictionary
# under $HOME (or $XDG_CONFIG_HOME), and several of these tests commit words or
# add dictionary entries. Giving each test its own directory keeps the user's
# real ~/.config/anthy out of it and lets `ctest -j` run them in parallel.
set(_home_root "${CMAKE_CURRENT_BINARY_DIR}/home")

# A writable copy of upstream's test data. test/main.c does not just read
# test.exp — it rewrites it at the end of every run with the results it saw.
# Pointing its SRCDIR at the submodule would modify a vendored tree, which this
# project never does, so it gets a copy that a CTest fixture refreshes before
# each run (see anthy.vendor.main.data below). The refresh also makes the test
# reproducible: without it, the second run would be graded against the first
# run's output rather than against upstream's reference data.
set(_vendor_data "${CMAKE_CURRENT_BINARY_DIR}/data")
file(MAKE_DIRECTORY "${_vendor_data}")

# Build one test executable against the anthy libraries, and reserve it a HOME.
function(_anthy_test_exe target)
  add_executable(${target} ${ARGN})
  target_include_directories(${target} PRIVATE
    "${ANTHY_TEST_SRC_DIR}"     # <anthy/anthy.h> and the internal headers
    "${ANTHY_TEST_CONFIG_DIR}"  # the port's generated <config.h>
    "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(${target} PRIVATE anthy-unicode anthydic-unicode)
  # anthy's own sources need the POSIX shims on Windows and so do these:
  # main.c calls alloca(), and the compat layer is where the mmap and POSIX
  # record-locking implementations live.
  libpathime_add_win32_compat(${target})
  # A test binary is useless before the dictionary exists.
  add_dependencies(${target} anthy-dic)
  # Every one of these sources — upstream's and ours — contains UTF-8 Japanese
  # string literals. MSVC and clang-cl otherwise read the source in the system
  # ANSI codepage, in which the lead bytes of those literals (0x81, 0x8d, ...)
  # are unmapped, and substitute silently. /utf-8 sets the source *and*
  # execution charset to UTF-8, matching what GCC and Clang already do.
  if(MSVC)
    target_compile_options(${target} PRIVATE /utf-8)
  endif()
  # Per-test HOME, named after the target so no two tests can share one.
  file(MAKE_DIRECTORY "${_home_root}/${target}")
  set(_test_home "${_home_root}/${target}" PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------------
# Upstream's suite (anthy-unicode/test/meson.build)
# ---------------------------------------------------------------------------
#
# SRCDIR and TEST_HOME are compiled in, exactly as meson does it. TEST_HOME
# must be absolute — main.c's own comment says a relative one misbehaves.

# main.c: reads test.txt, converts every line, and grades the results against
# test.exp. It links upstream's convdb helper, which the port builds for us
# (meson calls the same thing convdb_dep).
_anthy_test_exe(anthy_test_vendor_main "${_vendor_test}/main.c")
target_link_libraries(anthy_test_vendor_main PRIVATE anthy_convdb)
target_compile_definitions(anthy_test_vendor_main PRIVATE
  "SRCDIR=\"${_vendor_data}\"" "TEST_HOME=\"${_test_home}\"")

_anthy_test_exe(anthy_test_vendor_checklib "${_vendor_test}/check.c")
target_compile_definitions(anthy_test_vendor_checklib PRIVATE
  "SRCDIR=\"${_vendor_test}\"" "TEST_HOME=\"${_test_home}\"")

# prediction.c derives ANTHYDIR from SRCDIR as SRCDIR/../depgraph, so its
# SRCDIR has to stay inside the submodule. It only reads from there.
_anthy_test_exe(anthy_test_vendor_prediction "${_vendor_test}/prediction.c")
target_compile_definitions(anthy_test_vendor_prediction PRIVATE
  "SRCDIR=\"${_vendor_test}\"" "TEST_HOME=\"${_test_home}\"")

# test-matrix.c is pure computation over src-worddic/matrix.c: no dictionary,
# no working directory. It does exercise the byte-order conversion the on-disk
# dictionary format is built on, which is worth having on a little-endian
# platform reached by a different compiler.
_anthy_test_exe(anthy_test_vendor_matrix "${_vendor_test}/test-matrix.c")
target_compile_definitions(anthy_test_vendor_matrix PRIVATE
  "SRCDIR=\"${_vendor_test}\"" "TEST_HOME=\"${_test_home}\"")

# Refresh main.c's data before it runs. A CTest fixture rather than a build
# step, because the file is consumed *and* produced by the test itself.
add_test(NAME anthy.vendor.main.data
  COMMAND ${CMAKE_COMMAND} -E copy
          "${_vendor_test}/test.txt" "${_vendor_test}/test.exp"
          "${_vendor_data}")
set_tests_properties(anthy.vendor.main.data PROPERTIES
  FIXTURES_SETUP anthy_vendor_data)

add_test(NAME anthy.vendor.main
  COMMAND anthy_test_vendor_main --all
  WORKING_DIRECTORY "${ANTHY_TEST_RUNDIR}")
set_tests_properties(anthy.vendor.main PROPERTIES
  FIXTURES_REQUIRED anthy_vendor_data
  TIMEOUT 900)

add_test(NAME anthy.vendor.checklib
  COMMAND anthy_test_vendor_checklib
  WORKING_DIRECTORY "${ANTHY_TEST_RUNDIR}")

# Run with no argument, as meson does. Worth knowing before reading anything
# into a pass: prediction.c's main() begins `if (argc == 1) return 0;`, so
# upstream's invocation never reaches a prediction call — this only shows the
# program links and starts. The prediction API is covered for real by
# anthy.segment below.
add_test(NAME anthy.vendor.prediction
  COMMAND anthy_test_vendor_prediction
  WORKING_DIRECTORY "${ANTHY_TEST_RUNDIR}")

add_test(NAME anthy.vendor.matrix
  COMMAND anthy_test_vendor_matrix
  WORKING_DIRECTORY "${ANTHY_TEST_RUNDIR}")

# None of upstream's four programs ever returns a non-zero exit status; they
# report by printing. Left alone they would only catch a crash. Rather than
# modify the vendored sources, grade them on their output — these are the
# strings they and anthy's logger print when something has gone wrong.
set(_vendor_failure_re
    "fail \\(|failed to init|failed to open|Failed to open|Failed to init|Failed malloc|Failed fseek")
foreach(_t anthy.vendor.main anthy.vendor.checklib anthy.vendor.prediction)
  set_tests_properties(${_t} PROPERTIES
    FAIL_REGULAR_EXPRESSION "${_vendor_failure_re}"
    # anthy_get_user_dir() prefers XDG_CONFIG_HOME over the HOME these tests
    # override, and conf lookups fall back to getenv(). Without this, running
    # the suite on a Linux desktop writes into the real ~/.config/anthy.
    ENVIRONMENT "XDG_CONFIG_HOME=")
endforeach()

# test-matrix prints its error count (upstream's spelling of "dense" included).
# Requiring the zero turns an otherwise exit-code-only test into a real one.
set_tests_properties(anthy.vendor.matrix PROPERTIES
  PASS_REGULAR_EXPRESSION "0 errors in desnse matrix")

# main.c grades 558 conversions against upstream's reference verdicts and
# prints a summary. Most entries carry no verdict at all ("unknown"), but of
# those that do, the run has exactly one known miss — the same one on Linux and
# on Windows. Catch it getting worse. Deliberately not pinned to "miss : 1": a
# submodule bump that changes the dictionary or the corpus legitimately moves
# these numbers, and a test that cries wolf on every data update gets disabled.
# (APPEND, and after the loop above, because that loop assigns the property.)
set_property(TEST anthy.vendor.main APPEND PROPERTY
  FAIL_REGULAR_EXPRESSION "miss : [2-9]|miss : [0-9][0-9]")

# ---------------------------------------------------------------------------
# libpathime's own tests
# ---------------------------------------------------------------------------
#
# Absolute paths, no working-directory games, no environment, no reliance on a
# system temp directory, and a runtime check that the compiler read the source
# as UTF-8 — see anthy_test_util.h.

function(_anthy_own_test name source)
  set(_target "anthy_test_${name}")
  _anthy_test_exe(${_target} "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
  target_compile_definitions(${_target} PRIVATE
    "ANTHY_TEST_DIC=\"${ANTHY_DIC_FILE}\""
    "ANTHY_TEST_HOME=\"${_test_home}\"")
  add_test(NAME anthy.${name} COMMAND ${_target})
endfunction()

_anthy_own_test(convert test_convert.c)
_anthy_own_test(segment test_segment.c)
_anthy_own_test(dicutil test_dicutil.c)
