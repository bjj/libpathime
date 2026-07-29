# Native CMake port of pyzy / libpyzy (Chinese Pinyin & Bopomofo conversion).
#
# pyzy ships only autotools. This reproduces src/Makefile.am: the C++11 library
# plus its install artifacts. The three "built" headers (BopomofoKeyboard.h,
# PinyinParserTable.h, SimpTradConverterTable.h) are committed in src/, so no
# Python codegen is needed to build the library — Python is used only for the
# optional runtime android.db.

set(PYZY_SRC "${CMAKE_SOURCE_DIR}/engines/pyzy")

# --- config.h (HAVE_LIBUUID; HAVE_OPENCC intentionally left undefined) ------
set(HAVE_LIBUUID 1)   # libuuid on Unix; the Rpcrt4 shim on Windows
configure_file(config.h.in "${CMAKE_CURRENT_BINARY_DIR}/config.h" @ONLY)

# --- source directory ---------------------------------------------------------
# pyzy's sources compile in place from the submodule. Everything in src/ is
# included with quotes from co-located files, so they resolve against each
# other and src/ never has to go on the -I path — which would shadow the C
# library's <string.h> with pyzy's String.h on a case-insensitive filesystem.
set(_pyzy_dir "${PYZY_SRC}/src")

# Published for src/ and tests/pyzy, which stage pyzy's headers rather than put
# this directory on an -I path.
set(PYZY_SRC_DIR "${_pyzy_dir}" CACHE INTERNAL
  "The pyzy source directory this build compiles")

# --- library (source list from src/Makefile.am:libpyzy_c_sources) -----------
set(_pyzy_srcs
  BopomofoContext.cc Database.cc DataDir.cc DoublePinyinContext.cc
  DynamicSpecialPhrase.cc FullPinyinContext.cc InputContext.cc
  PhoneticContext.cc PhraseEditor.cc PinyinContext.cc PinyinParser.cc
  SimpTradConverter.cc SpecialPhraseTable.cc Variant.cc)
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
target_compile_definitions(pyzy PRIVATE HAVE_CONFIG_H)
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
# GLOBAL: pyzy links glib PRIVATE, but glib is in pyzy's *public* private-header
# surface — String.h calls g_strdup_vprintf inline — so tests/pyzy needs the
# same imported target. Without GLOBAL, pkg_check_modules scopes it to this
# directory and the test tree would have to probe glib a second time.
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET GLOBAL glib-2.0>=2.24.0)
target_link_libraries(pyzy PRIVATE PkgConfig::GLIB)

if(WIN32)
  # Supplies <uuid/uuid.h> + <sys/utsname.h> shims and links Rpcrt4.
  libpathime_add_win32_compat(pyzy)
else()
  # GLOBAL for the same reason as glib above: Util.h includes <uuid/uuid.h>
  # unconditionally, so tests/pyzy needs whatever include path found it here.
  pkg_check_modules(UUID REQUIRED IMPORTED_TARGET GLOBAL uuid)
  target_link_libraries(pyzy PRIVATE PkgConfig::UUID)
endif()

# --- optional runtime database (android.db) ---------------------------------
option(PYZY_BUILD_DB_ANDROID "Build pyzy's bundled Android pinyin database" ON)
if(PYZY_BUILD_DB_ANDROID)
  # Shared with the coverage-map regeneration target; the Windows fallback it
  # carries is in cmake/LibpathimeDependencies.cmake.
  libpathime_find_python3(pyzy)

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
    # Published for cmake/LibpathimeRuntimeData.cmake, which installs it as
    # main.db under pathime-data/pyzy, and for tests/pyzy. Empty when there is
    # no database, which is a supported configuration: pyzy still builds and
    # the Chinese engines report themselves unavailable.
    set(PYZY_ANDROID_DB "${_db}/android.db" CACHE INTERNAL
      "Path to the android.db this build produces, or empty if it was skipped")
  else()
    message(WARNING
      "pyzy: Python 3 not found — skipping android.db (runtime conversion data). "
      "The library still builds; set PYZY_BUILD_DB_ANDROID=OFF to silence this.")
    set(PYZY_ANDROID_DB "" CACHE INTERNAL
      "Path to the android.db this build produces, or empty if it was skipped")
  endif()
else()
  set(PYZY_ANDROID_DB "" CACHE INTERNAL
    "Path to the android.db this build produces, or empty if it was skipped")
endif()

# --- install ----------------------------------------------------------------
install(TARGETS pyzy
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES
  "${_pyzy_dir}/Const.h" "${_pyzy_dir}/DataDir.h"
  "${_pyzy_dir}/InputContext.h" "${_pyzy_dir}/Variant.h"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/pyzy-1.0/PyZy")
# phrases.txt is not installed here, for the same reason android.db is not:
# libpathime tells pyzy where to look, so the copy that matters is the one
# under pathime-data/ that cmake/LibpathimeRuntimeData.cmake installs.
set(PYZY_PHRASES_FILE "${PYZY_SRC}/src/phrases.txt" CACHE INTERNAL
  "pyzy's stock special-phrase table")
