# Chinese backend (pyzy) tests.
#
# ---------------------------------------------------------------------------
# Why pyzy's own suite is not wired up here
# ---------------------------------------------------------------------------
# pyzy ships exactly one test, src/tests/basic.cc, and it has not compiled for
# a long time. Its DummyObserver declares the six InputContext::Observer
# callbacks taking `const InputContext *`, where the interface declares them
# taking `InputContext *`. So none of them override anything, DummyObserver
# stays abstract, and the file fails where it tries to instantiate one. That is
# an upstream bug in the test, not something this build caused.
#
# It could be worked around: the port's mechanism for unbuildable vendored
# sources (mirror into the build tree, rewrite the copy, guard the rewrite with
# a FATAL_ERROR) would need one `string(REPLACE "const InputContext *context"
# "InputContext *context")` here. Two reasons not to.
#
# The first is what that rewrite would be doing. Every other rewrite in this
# port repairs a *library* source so it compiles as its author meant it to.
# This one would have the build system edit the declarations of a test, which
# is the one kind of file whose text is the thing being trusted.
#
# The second is that nobody knows what basic.cc asserts any more. It cannot
# have been run since the signature changed, and much of it is pinned to
# candidate ordering in a particular android.db — selectCandidate(4) on "aazhi"
# expecting 阿, and so on. Wiring up a suite that has never passed anywhere
# would produce a red test whose failures say nothing about this port.
#
# Instead the behaviour basic.cc was written to check is covered by the tests
# below, and the expectations are taken from basic.cc itself wherever it has
# one. That is deliberate: those values describe how pyzy behaves on Linux with
# the bundled android.db, which turns them into a cross-platform comparison
# rather than a recording of whatever this machine happens to do. Where a test
# asserts something basic.cc does not cover, the source says so.
#
# Every test here must build and pass on Linux as well. A test that only runs
# on Windows cannot tell us whether Windows behaves like Linux, which is the
# entire reason this directory exists.

if(NOT PYZY_EFFECTIVE_SRC_DIR)
  message(FATAL_ERROR
    "tests/pyzy: PYZY_EFFECTIVE_SRC_DIR is not set. It is published by "
    "cmake/ports/pyzy/CMakeLists.txt, which must be processed first.")
endif()

# --- pyzy's headers, without putting its src/ on the include path -----------
#
# src/String.h shadows the C library's <string.h> on a case-insensitive
# filesystem, so pyzy's source directory must never appear in an -I. Two ways
# around that, one per kind of header:
#
#   * the three *public* headers get staged into the layout `cmake --install`
#     produces, so tests include them exactly as an outside consumer would:
#     <PyZy/InputContext.h>. The staging directory holds nothing else, so
#     there is nothing for a system header to collide with.
#   * the *private* headers are reached through generated forwarding headers
#     that #include them by absolute path. Their own quoted includes then
#     resolve relative to themselves, as they do when pyzy compiles itself.
#
# Both are configure-time copies of ${PYZY_EFFECTIVE_SRC_DIR} — on Windows the
# rewritten mirror, not pyzy/src. Building a test against the raw submodule
# headers there would give it a String with a different operator<< set from
# the one inside the DLL.
file(COPY
  "${PYZY_EFFECTIVE_SRC_DIR}/Const.h"
  "${PYZY_EFFECTIVE_SRC_DIR}/InputContext.h"
  "${PYZY_EFFECTIVE_SRC_DIR}/Variant.h"
  DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/include/PyZy")

configure_file(pyzy_private.h.in      "${CMAKE_CURRENT_BINARY_DIR}/pyzy_private.h"      @ONLY)
configure_file(pyzy_pinyin_table.h.in "${CMAKE_CURRENT_BINARY_DIR}/pyzy_pinyin_table.h" @ONLY)

# pyzy's private headers include <config.h>, which the port generates into its
# own binary directory.
get_target_property(_pyzy_port_bindir pyzy BINARY_DIR)

# --- runtime data -----------------------------------------------------------
#
# PyZy::Database::open() searches PKGDATADIR (an install path that does not
# exist in a build tree) and then "main.db" relative to the process's working
# directory; SpecialPhraseTable looks for "phrases.txt" in the working
# directory before anywhere else. So a conversion test needs a working
# directory with both staged into it, and gets its own so that a run with
# `ctest -j` does not have several processes fighting over one SQLite file
# opened with PRAGMA locking_mode=EXCLUSIVE.
set(_pyzy_phrases "${CMAKE_SOURCE_DIR}/pyzy/src/phrases.txt")

# pyzy_add_test(<name> [NEEDS_DB] <source>...)
#
# Registers ctest name `pyzy.<name>` built from <source>. NEEDS_DB stages
# android.db and phrases.txt into the test's working directory and makes the
# target depend on the database being built.
function(pyzy_add_test name)
  cmake_parse_arguments(ARG "NEEDS_DB" "" "" ${ARGN})
  set(_target "pyzy_${name}_test")
  set(_rundir "${CMAKE_CURRENT_BINARY_DIR}/run/${name}")

  add_executable(${_target} ${ARG_UNPARSED_ARGUMENTS})

  target_include_directories(${_target} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"                 # pyzy_assert.h, pyzy_context.h
    "${CMAKE_CURRENT_BINARY_DIR}"                 # the generated forwarding headers
    "${CMAKE_CURRENT_BINARY_DIR}/include"         # <PyZy/...>
    "${_pyzy_port_bindir}")                       # the port's generated <config.h>

  # glib: String.h calls g_strdup_vprintf inline, so anything including a pyzy
  # private header compiles against glib whether or not it uses it directly.
  target_link_libraries(${_target} PRIVATE libpathime::pyzy PkgConfig::GLIB)
  target_compile_definitions(${_target} PRIVATE HAVE_CONFIG_H)

  if(MSVC)
    # Both of these must match how the library itself was compiled. Without
    # __GXX_EXPERIMENTAL_CXX0X__, Util.h takes its boost fallback and every
    # pyzy type that holds a smart pointer changes shape; without /utf-8, MSVC
    # decodes this directory's UTF-8 sources as the host ANSI codepage and the
    # expected Chinese strings become mojibake that can never match.
    target_compile_definitions(${_target} PRIVATE __GXX_EXPERIMENTAL_CXX0X__)
    target_compile_options(${_target} PRIVATE /utf-8)
  endif()

  # Util.h includes <uuid/uuid.h> and <sys/utsname.h> unconditionally, so every
  # test that touches a private header needs them: from the compat shims on
  # Windows, from libuuid (and the C library) elsewhere.
  if(WIN32)
    libpathime_add_win32_compat(${_target})
  else()
    target_link_libraries(${_target} PRIVATE PkgConfig::UUID)
  endif()

  file(MAKE_DIRECTORY "${_rundir}")
  if(ARG_NEEDS_DB)
    add_dependencies(${_target} pyzy-db)
    add_custom_command(TARGET ${_target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${PYZY_ANDROID_DB}" "${_rundir}/main.db"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${_pyzy_phrases}" "${_rundir}/phrases.txt"
      COMMENT "pyzy.${name}: staging android.db and phrases.txt"
      VERBATIM)
  endif()

  add_test(NAME "pyzy.${name}"
           COMMAND ${_target}
           WORKING_DIRECTORY "${_rundir}")
endfunction()

# --- tests that need no data ------------------------------------------------
# Pure table lookup: no database, no InputContext, nothing to stage.
pyzy_add_test(pinyin_parser pinyin_parser_test.cc)
pyzy_add_test(simp_trad     simp_trad_test.cc)

# --- conversion tests -------------------------------------------------------
# These need android.db, which the port only builds when it can find a Python 3
# interpreter. Registering them without it would give three tests that fail for
# a reason unrelated to anything they are meant to check, so say so instead.
if(PYZY_ANDROID_DB)
  pyzy_add_test(full_pinyin   NEEDS_DB full_pinyin_test.cc)
  pyzy_add_test(double_pinyin NEEDS_DB double_pinyin_test.cc)
  pyzy_add_test(bopomofo      NEEDS_DB bopomofo_test.cc)
else()
  message(WARNING
    "tests/pyzy: android.db was not built, so the conversion tests "
    "(pyzy.full_pinyin, pyzy.double_pinyin, pyzy.bopomofo) are not registered. "
    "They need Python 3 at configure time; see PYZY_BUILD_DB_ANDROID.")
endif()
