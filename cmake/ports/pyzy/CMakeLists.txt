# Native CMake port of pyzy / libpyzy (Chinese Pinyin & Bopomofo conversion).
#
# pyzy ships only autotools. This reproduces src/Makefile.am: the C++11 library
# plus its install artifacts. The three "built" headers (BopomofoKeyboard.h,
# PinyinParserTable.h, SimpTradConverterTable.h) are committed in src/, so no
# Python codegen is needed to build the library — Python is used only for the
# optional runtime android.db.

set(PYZY_SRC "${CMAKE_SOURCE_DIR}/pyzy")

# --- config.h (HAVE_LIBUUID; HAVE_OPENCC intentionally left undefined) ------
set(HAVE_LIBUUID 1)   # libuuid on Unix; the Rpcrt4 shim on Windows
configure_file(config.h.in "${CMAKE_CURRENT_BINARY_DIR}/config.h" @ONLY)

# --- source directory (mirrored and fixed up on Windows) --------------------
# Three things in pyzy's sources are GCC/LP64-isms that no MSVC-style compiler
# will accept. Rather than edit the submodule, mirror src/ into the build tree
# and fix the copies. Everything in src/ is included with quotes from
# co-located files, so the mirror resolves against itself and src/ still never
# has to go on the -I path (which would shadow <string.h> with pyzy's String.h
# on a case-insensitive filesystem).
#
#   1. PinyinParserTable.h initialises its ~400-entry table with GNU labelled
#      fields (`text : "a",`) — an obsolete GCC extension. The labels appear in
#      struct-declaration order, so dropping them leaves valid aggregate
#      initialisation.
#   2. String.h overloads operator<< for int / unsigned int / unsigned long
#      only, which under LP64 happens to cover every integer type pyzy streams.
#      On Windows it does not, and two kinds of ambiguity appear in
#      Database.cc's SQL building:
#        * size_t is unsigned long long, so `sql << i` sits equidistant from
#          all three overloads;
#        * for the narrow types (`p.pinyin_id[i].sheng` is unsigned char) MSVC
#          throws a built-in `operator<<(bool, unsigned char)` into the
#          candidate set — reachable because String has an implicit
#          `operator const char *` — which costs one user-defined conversion on
#          the left where the member `operator<<(int)` costs a promotion on the
#          right, so neither wins.
#      Both are fixed by widening the overload set with exact matches.
#   3. PhraseEditor.h forward-declares `class Config;` where Config.h says
#      `struct Config` — invisible under the Itanium ABI, a link error under
#      MSVC's class-key-sensitive mangling.
#
# The mirror is produced at configure time: editing pyzy/src needs a re-run of
# cmake to take effect.
set(_pyzy_dir "${PYZY_SRC}/src")

if(WIN32)
  set(_pyzy_dir "${CMAKE_CURRENT_BINARY_DIR}/src-win32")
  file(GLOB _pyzy_mirror_files "${PYZY_SRC}/src/*.h" "${PYZY_SRC}/src/*.cc")
  file(COPY ${_pyzy_mirror_files} DESTINATION "${_pyzy_dir}")

  file(READ "${_pyzy_dir}/PinyinParserTable.h" _tbl)
  string(REGEX REPLACE
    "\n([ \t]+)(text|bopomofo|sheng|yun|pinyin_id|len|flags)[ \t]*:[ \t]*"
    "\n\\1/* \\2 */ " _tbl_patched "${_tbl}")
  if(_tbl_patched STREQUAL _tbl)
    message(FATAL_ERROR
      "pyzy: PinyinParserTable.h has no GNU labelled-field initialisers to "
      "rewrite — the vendored source has changed shape.")
  endif()
  file(WRITE "${_pyzy_dir}/PinyinParserTable.h" "${_tbl_patched}")

  # PhraseEditor.h forward-declares Config with the wrong class-key
  # (`class Config;` against Config.h's `struct Config`). The Itanium ABI does
  # not encode the class-key so GCC never notices; MSVC does, and
  # PhraseEditor's constructor ends up defined and referenced under two
  # different mangled names.
  file(READ "${_pyzy_dir}/PhraseEditor.h" _pe_h)
  string(REPLACE "class Config;" "struct Config;" _pe_h_patched "${_pe_h}")
  if(_pe_h_patched STREQUAL _pe_h)
    message(FATAL_ERROR
      "pyzy: could not find PhraseEditor.h's `class Config;` forward "
      "declaration to correct — the vendored source has changed shape.")
  endif()
  file(WRITE "${_pyzy_dir}/PhraseEditor.h" "${_pe_h_patched}")

  file(READ "${_pyzy_dir}/String.h" _str_h)
  string(REGEX REPLACE
    "(String & operator<< \\(unsigned long i\\)[^}]*\\})"
    [[\1

    /* Grafted in by libpathime's Windows port; see cmake/ports/pyzy for why.
     * Every one of these routes to the overload the same expression picks on
     * Linux, so the rendered SQL is byte-identical across platforms. */
    String & operator<< (long long i)
    {
        return appendPrintf ("%lld", i);
    }

    String & operator<< (unsigned long long i)
    {
        return appendPrintf ("%llu", i);
    }

    String & operator<< (signed char i)    { return operator<< ((int) i); }
    String & operator<< (unsigned char i)  { return operator<< ((int) i); }
    String & operator<< (short i)          { return operator<< ((int) i); }
    String & operator<< (unsigned short i) { return operator<< ((int) i); }]]
    _str_h_patched "${_str_h}")
  if(_str_h_patched STREQUAL _str_h)
    message(FATAL_ERROR
      "pyzy: could not graft the extra operator<< overloads onto String.h "
      "— the vendored source has changed shape.")
  endif()
  file(WRITE "${_pyzy_dir}/String.h" "${_str_h_patched}")
endif()

# --- library (source list from src/Makefile.am:libpyzy_c_sources) -----------
set(_pyzy_srcs
  BopomofoContext.cc Database.cc DoublePinyinContext.cc DynamicSpecialPhrase.cc
  FullPinyinContext.cc InputContext.cc PhoneticContext.cc PhraseEditor.cc
  PinyinContext.cc PinyinParser.cc SimpTradConverter.cc SpecialPhraseTable.cc
  Variant.cc)
list(TRANSFORM _pyzy_srcs PREPEND "${_pyzy_dir}/")

add_library(pyzy ${_pyzy_srcs})
add_library(libpathime::pyzy ALIAS pyzy)
set_target_properties(pyzy PROPERTIES
  OUTPUT_NAME pyzy-1.0    # matches the autotools/pkg-config name libpyzy-1.0
  VERSION 1.0.0
  SOVERSION 1)

# Note: pyzy's own headers (incl. the committed generated ones) are included
# with quotes from the co-located .cc files, so src/ resolves relatively and
# must NOT go on the -I path — doing so shadows the C library <string.h> with
# pyzy's src/String.h on case-insensitive filesystems (Windows, macOS).
target_include_directories(pyzy PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}")      # for our generated <config.h> only
target_compile_definitions(pyzy PRIVATE
  HAVE_CONFIG_H
  "PKGDATADIR=\"${CMAKE_INSTALL_FULL_DATADIR}/pyzy\"")
# Util.h takes the std::shared_ptr path only when __GXX_EXPERIMENTAL_CXX0X__ is
# defined, otherwise it falls back to boost. GCC and Clang's GNU driver
# predefine it in C++11 mode; neither MSVC nor clang-cl does, and we require
# C++11 project-wide, so define it for every MSVC-style frontend. (`MSVC` is
# true for clang-cl too, unlike CXX_COMPILER_ID.)
if(MSVC)
  target_compile_definitions(pyzy PRIVATE __GXX_EXPERIMENTAL_CXX0X__)
  # The Bopomofo tables are UTF-8 wide string literals (L"ㄚ"). Without /utf-8
  # MSVC decodes the source as the host ANSI codepage and they come out
  # mojibake; clang-cl accepts the flag and already assumes UTF-8.
  target_compile_options(pyzy PRIVATE /utf-8)
endif()

# --- dependencies -----------------------------------------------------------
find_package(SQLite3 REQUIRED)
target_link_libraries(pyzy PRIVATE SQLite::SQLite3)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0>=2.24.0)
target_link_libraries(pyzy PRIVATE PkgConfig::GLIB)

if(WIN32)
  # Supplies <uuid/uuid.h> + <sys/utsname.h> shims and links Rpcrt4.
  libpathime_add_win32_compat(pyzy)
else()
  pkg_check_modules(UUID REQUIRED IMPORTED_TARGET uuid)
  target_link_libraries(pyzy PRIVATE PkgConfig::UUID)
endif()

# --- optional runtime database (android.db) ---------------------------------
option(PYZY_BUILD_DB_ANDROID "Build pyzy's bundled Android pinyin database" ON)
if(PYZY_BUILD_DB_ANDROID)
  find_package(Python3 COMPONENTS Interpreter QUIET)

  # FindPython3 misses plenty of real Windows installs: the PATH entry is often
  # the Store's stub launcher, and per-user registry entries written by conda &
  # co. lack the values FindPython3 validates against. py.exe is the platform's
  # own answer to "where is Python", so ask it before giving up.
  if(NOT Python3_Interpreter_FOUND AND WIN32)
    find_program(PYZY_PY_LAUNCHER py)
    if(PYZY_PY_LAUNCHER)
      execute_process(
        COMMAND "${PYZY_PY_LAUNCHER}" -3 -c "import sys; sys.stdout.write(sys.executable)"
        OUTPUT_VARIABLE _py_exe RESULT_VARIABLE _py_rc ERROR_QUIET)
      if(_py_rc EQUAL 0 AND EXISTS "${_py_exe}")
        message(STATUS "pyzy: using Python 3 via the py launcher: ${_py_exe}")
        set(Python3_EXECUTABLE "${_py_exe}")
        set(Python3_Interpreter_FOUND TRUE)
      endif()
    endif()
  endif()

  if(Python3_Interpreter_FOUND)
    set(_db "${CMAKE_CURRENT_BINARY_DIR}/db")
    file(MAKE_DIRECTORY "${_db}")
    add_custom_command(
      OUTPUT "${_db}/android.db"
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/gen_android_db.py"
              "${PYZY_SRC}/data/db/android"
              "${PYZY_SRC}/data/db/android/rawdict_utf16_65105_freq.txt"
              "${_db}/android.db"
              "${PYZY_SRC}/data/db/create_index.sql"
      DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/gen_android_db.py"
              "${PYZY_SRC}/data/db/android/create_db.py"
              "${PYZY_SRC}/data/db/android/rawdict_utf16_65105_freq.txt"
              "${PYZY_SRC}/data/db/create_index.sql"
      COMMENT "pyzy: building android.db"
      VERBATIM)
    add_custom_target(pyzy-db ALL DEPENDS "${_db}/android.db")
    install(FILES "${_db}/android.db"
      DESTINATION "${CMAKE_INSTALL_DATADIR}/pyzy/db")
  else()
    message(WARNING
      "pyzy: Python 3 not found — skipping android.db (runtime conversion data). "
      "The library still builds; set PYZY_BUILD_DB_ANDROID=OFF to silence this.")
  endif()
endif()

# --- install ----------------------------------------------------------------
install(TARGETS pyzy
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES
  "${PYZY_SRC}/src/Const.h" "${PYZY_SRC}/src/InputContext.h" "${PYZY_SRC}/src/Variant.h"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/pyzy-1.0/PyZy")
install(FILES "${PYZY_SRC}/src/phrases.txt"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/pyzy")
