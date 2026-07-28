# Korean backend (libhangul) tests.
#
# Two groups, per the convention in ../CMakeLists.txt:
#
#   hangul.vendored.*  libhangul's own test programs, wired up here because
#                      the top-level build descends into engines/libhangul/hangul
#                      rather than the libhangul top level (whose CMakeLists
#                      pulls in po/ and needs msgfmt), so upstream's
#                      engines/libhangul/test/CMakeLists.txt is never processed.
#   hangul.<name>      tests written for libpathime, whose job is to make the
#                      Windows build prove it behaves like the Linux one.
#
# Nothing under engines/libhangul/ is modified: the vendored sources are compiled from
# where they sit, with the compile definitions upstream's test/CMakeLists.txt
# gives them (retargeted at this tree's paths).

# libhangul's `hangul` target publishes no interface include directory — its
# own CMakeLists only sets a PRIVATE one — so every consumer has to name the
# header directory itself. Upstream's tests dodge this with "../hangul/hangul.h";
# ours use <hangul.h>, which is also how an installed libhangul is consumed.
set(_hangul_include_dir "${PROJECT_SOURCE_DIR}/engines/libhangul/hangul")

set(_hanja_txt "${PROJECT_SOURCE_DIR}/engines/libhangul/data/hanja/hanja.txt")

# ---------------------------------------------------------------------------
# Group 1: the vendored programs.
#
# Neither hangul.c nor hanja.c is self-checking — both are stdin -> stdout
# filters meant to be driven by hand — so each is paired with run_filter_test.cmake,
# which feeds it a fixed input and diffs the result. The input and expected
# files are run through configure_file(NEWLINE_STYLE LF) rather than used in
# place: the machines this repo is developed on have core.autocrlf=true, and
# hanja.c strips only '\n' from the keys it reads, so a CRLF checkout would
# leave a stray CR in every lookup key and nothing would ever match.
# ---------------------------------------------------------------------------

set(_gen "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${_gen}")

foreach(_f hanja-dict hanja-input hanja-expected hangul-input hangul-expected)
  configure_file("${CMAKE_CURRENT_SOURCE_DIR}/data/${_f}.txt.in"
                 "${_gen}/${_f}.txt" @ONLY NEWLINE_STYLE LF)
endforeach()

# --- hanja.c: dictionary load + prefix match, driven from stdin.
add_executable(hangul_vendored_hanja
  "${PROJECT_SOURCE_DIR}/engines/libhangul/test/hanja.c")
target_link_libraries(hangul_vendored_hanja PRIVATE hangul)
# Upstream points TEST_HANJA_TXT at ${CMAKE_SOURCE_DIR}/data/hanja/hanja.txt,
# which is correct when libhangul is the top-level project and wrong here. It
# is only the fallback for a missing argv[1] — the test passes its own small
# dictionary — but leaving it pointing at a nonexistent path would turn a
# dropped argument into a silent NULL table rather than a visible failure.
target_compile_definitions(hangul_vendored_hanja PRIVATE
  TEST_HANJA_TXT=\"${_hanja_txt}\")

add_test(NAME hangul.vendored.hanja
  COMMAND "${CMAKE_COMMAND}"
    "-DEXE=$<TARGET_FILE:hangul_vendored_hanja>"
    "-DEXE_ARG=${_gen}/hanja-dict.txt"
    "-DINPUT=${_gen}/hanja-input.txt"
    "-DEXPECTED=${_gen}/hanja-expected.txt"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/run_filter_test.cmake")

# --- hangul.c: the automata end to end, ascii in, UTF-8 out.
#
# This one needs iconv to turn libhangul's UCS-4 output into UTF-8. glibc has
# it built in, so Linux always gets it. Windows has no system iconv and there
# is no shim for one in cmake/compat/win32 (nothing in the three submodules
# needs it) — but a Windows build configured with the vcpkg toolchain does
# find vcpkg's libiconv, which glib pulls in anyway, so the usual full build
# runs this test there too. Making iconv a hard requirement for a demo program
# would be the wrong trade, so the test is simply registered where iconv
# exists: hangul_ic_* behaviour is covered portably, and far more thoroughly,
# by hangul.ic below.
find_package(Iconv QUIET)
if(Iconv_FOUND)
  add_executable(hangul_vendored_hangul
    "${PROJECT_SOURCE_DIR}/engines/libhangul/test/hangul.c")
  target_link_libraries(hangul_vendored_hangul PRIVATE hangul Iconv::Iconv)
  # Inert here: the source only calls hangul_init() under
  # ENABLE_EXTERNAL_KEYBOARDS, which the top-level CMakeLists turns off (it
  # would drag in EXPAT and a sed-based codegen step), so no data/keyboards
  # directory is produced and none is needed. Defined anyway so the target
  # still compiles if that option is ever turned back on.
  target_compile_definitions(hangul_vendored_hangul PRIVATE
    TEST_LIBHANGUL_KEYBOARD_PATH=\"${PROJECT_BINARY_DIR}/data/keyboards\")

  add_test(NAME hangul.vendored.hangul
    COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:hangul_vendored_hangul>"
      "-DINPUT=${_gen}/hangul-input.txt"
      "-DEXPECTED=${_gen}/hangul-expected.txt"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/run_filter_test.cmake")
else()
  message(STATUS
    "libpathime: hangul.vendored.hangul skipped — no iconv "
    "(engines/libhangul/test/hangul.c uses it to print UCS-4 as UTF-8)")
endif()

# --- test.c: upstream's Check suite.
#
# Two things have to be true before this is worth building.
#
# 1. Check itself, found through pkg-config as upstream does. It is packaged
#    everywhere on Linux and essentially nowhere on Windows, so this is the
#    usual reason the suite is absent.
# 2. A 32-bit wchar_t. test.c compares libhangul's UCS-4 output by casting it,
#      wcscmp((const wchar_t*)hangul_ic_get_preedit_string(ic), L"...")
#    which is only meaningful where wchar_t is as wide as ucschar. On Windows
#    it is 16 bits: each UCS-4 code point is read as two UTF-16 units, the
#    high one zero for anything in the BMP, so wcscmp compares one character
#    and stops. The suite would not be testing what it says it tests. This is
#    an upstream bug, not a gap in this port — and it is precisely why the
#    hangul.ic test below re-states the same expectations against ucschar.
include(CheckTypeSize)
check_type_size(wchar_t LIBPATHIME_SIZEOF_WCHAR_T LANGUAGE C)

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(CHECK QUIET check)
endif()

if(CHECK_FOUND AND LIBPATHIME_SIZEOF_WCHAR_T EQUAL 4)
  add_executable(hangul_vendored_unittest
    "${PROJECT_SOURCE_DIR}/engines/libhangul/test/test.c")
  target_compile_definitions(hangul_vendored_unittest PRIVATE
    TEST_LIBHANGUL_KEYBOARD_PATH=\"${PROJECT_BINARY_DIR}/data/keyboards\"
    TEST_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}/engines/libhangul/test\")
  target_include_directories(hangul_vendored_unittest PRIVATE ${CHECK_INCLUDE_DIRS})
  target_link_libraries(hangul_vendored_unittest PRIVATE hangul ${CHECK_LDFLAGS})
  add_test(NAME hangul.vendored.unittest
    COMMAND hangul_vendored_unittest)
elseif(NOT CHECK_FOUND)
  message(STATUS "libpathime: hangul.vendored.unittest skipped — Check not found")
else()
  message(STATUS
    "libpathime: hangul.vendored.unittest skipped — wchar_t is "
    "${LIBPATHIME_SIZEOF_WCHAR_T} bytes; upstream's test.c casts ucschar* to "
    "wchar_t* (see comment in tests/hangul/CMakeLists.txt). hangul.ic covers "
    "the same expectations portably.")
endif()

# ---------------------------------------------------------------------------
# Group 2: libpathime's own tests. Plain C11 against the public header, no
# external test dependency, identical on both platforms.
# ---------------------------------------------------------------------------

function(libpathime_add_hangul_test name source)
  add_executable(${name} "${source}")
  target_link_libraries(${name} PRIVATE hangul)
  target_include_directories(${name} PRIVATE
    "${_hangul_include_dir}" "${CMAKE_CURRENT_SOURCE_DIR}")
endfunction()

libpathime_add_hangul_test(hangul_ic_test hangul_ic_test.c)
add_test(NAME hangul.ic COMMAND hangul_ic_test)

libpathime_add_hangul_test(hangul_ucs4_test hangul_ucs4_test.c)
add_test(NAME hangul.ucs4 COMMAND hangul_ucs4_test)

# The hanja test writes dictionaries with hand-chosen line endings, so it needs
# somewhere to put them. Give it a directory in the build tree rather than let
# it reach for a system temp path, which differs between the platforms and is
# the sort of thing that quietly works on one and not the other.
set(_hanja_scratch "${CMAKE_CURRENT_BINARY_DIR}/hanja-scratch")
file(MAKE_DIRECTORY "${_hanja_scratch}")

libpathime_add_hangul_test(hangul_hanja_test hangul_hanja_test.c)
add_test(NAME hangul.hanja
  COMMAND hangul_hanja_test "${_hanja_txt}" "${_hanja_scratch}")
