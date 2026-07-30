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
# It could be fixed the way every other vendored defect here is — a commit on
# pyzy's `libpathime` branch correcting the six signatures. Two reasons not to.
#
# The first is what that commit would be changing. The others repair a
# *library* source so it compiles as its author meant it to. This one would
# edit the declarations of a test, which is the one kind of file whose text is
# the thing being trusted.
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

if(NOT PYZY_SRC_DIR)
  message(FATAL_ERROR
    "tests/pyzy: PYZY_SRC_DIR is not set. It is published by "
    "cmake/ports/pyzy/CMakeLists.txt, which must be processed first.")
endif()

# --- pyzy's headers, without putting its src/ on the include path -----------
#
# src/String.h shadows the C library's <string.h> on a case-insensitive
# filesystem, so pyzy's source directory must never appear in an -I. Two ways
# around that, one per kind of header:
#
#   * the *public* headers get staged into the layout `cmake --install`
#     produces, so tests include them exactly as an outside consumer would:
#     <PyZy/InputContext.h>. The staging directory holds nothing else, so
#     there is nothing for a system header to collide with. DataDir.h is among
#     them: it is how a program says where the data is.
#   * the *private* headers are reached through generated forwarding headers
#     that #include them by absolute path. Their own quoted includes then
#     resolve relative to themselves, as they do when pyzy compiles itself.
file(COPY
  "${PYZY_SRC_DIR}/Const.h"
  "${PYZY_SRC_DIR}/DataDir.h"
  "${PYZY_SRC_DIR}/InputContext.h"
  "${PYZY_SRC_DIR}/Variant.h"
  DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/include/PyZy")

configure_file(pyzy_private.h.in      "${CMAKE_CURRENT_BINARY_DIR}/pyzy_private.h"      @ONLY)
configure_file(pyzy_pinyin_table.h.in "${CMAKE_CURRENT_BINARY_DIR}/pyzy_pinyin_table.h" @ONLY)

# pyzy's private headers include <config.h>, which the port generates into its
# own binary directory.
get_target_property(_pyzy_port_bindir pyzy BINARY_DIR)

# --- runtime data -----------------------------------------------------------
#
# The conversion tests need the database pyzy reads, under the name
# Database::open() asks for. One staged copy serves all of them: they name it
# through pyzy_set_data_dir() and only ever read it, so there is nothing for
# processes under `ctest -j` to contend over. What each test does *write* — the
# user cache and config pyzy keeps — goes in a directory of its own.
if(PYZY_ANDROID_DB)
  set(_pyzy_data "${CMAKE_CURRENT_BINARY_DIR}/data")
  add_custom_command(
    OUTPUT "${_pyzy_data}/main.db" "${_pyzy_data}/phrases.txt"
    COMMAND ${CMAKE_COMMAND} -E copy "${PYZY_ANDROID_DB}" "${_pyzy_data}/main.db"
    COMMAND ${CMAKE_COMMAND} -E copy
            "${PYZY_PHRASES_FILE}" "${_pyzy_data}/phrases.txt"
    DEPENDS "${PYZY_ANDROID_DB}" "${PYZY_PHRASES_FILE}"
    COMMENT "tests/pyzy: staging android.db and phrases.txt"
    VERBATIM)
  add_custom_target(pyzy-test-data
    DEPENDS "${_pyzy_data}/main.db" "${_pyzy_data}/phrases.txt")
  add_dependencies(pyzy-test-data pyzy-db)
endif()

# pyzy_add_test(<name> [NEEDS_DB] <source>...)
#
# Registers ctest name `pyzy.<name>` built from <source>. Every test gets
# PYZY_TEST_HOME, the directory it may write to; NEEDS_DB additionally gives it
# PYZY_TEST_DATA_DIR, the staged database, and makes it wait for that staging.
function(pyzy_add_test name)
  cmake_parse_arguments(ARG "NEEDS_DB" "" "" ${ARGN})
  set(_target "pyzy_${name}_test")
  set(_home "${CMAKE_CURRENT_BINARY_DIR}/home/${name}")

  add_executable(${_target} ${ARG_UNPARSED_ARGUMENTS})

  target_include_directories(${_target} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"                 # pyzy_assert.h, pyzy_context.h
    "${CMAKE_CURRENT_BINARY_DIR}"                 # the generated forwarding headers
    "${CMAKE_CURRENT_BINARY_DIR}/include"         # <PyZy/...>
    "${_pyzy_port_bindir}")                       # the port's generated <config.h>

  # glib: String.h calls g_strdup_vprintf inline, so anything including a pyzy
  # private header compiles against glib whether or not it uses it directly.
  target_link_libraries(${_target} PRIVATE libpathime::pyzy PkgConfig::LIBPATHIME_GLIB)
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
    target_link_libraries(${_target} PRIVATE PkgConfig::LIBPATHIME_UUID)
  endif()

  file(MAKE_DIRECTORY "${_home}")
  target_compile_definitions(${_target} PRIVATE "PYZY_TEST_HOME=\"${_home}\"")

  if(ARG_NEEDS_DB)
    add_dependencies(${_target} pyzy-test-data)
    target_compile_definitions(${_target} PRIVATE
      "PYZY_TEST_DATA_DIR=\"${_pyzy_data}\"")
  endif()

  # No WORKING_DIRECTORY: every path these tests use is absolute and passed to
  # pyzy explicitly, so where ctest starts them makes no difference.
  add_test(NAME "pyzy.${name}" COMMAND ${_target})
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
