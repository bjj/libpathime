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

# --- library (source list from src/Makefile.am:libpyzy_c_sources) -----------
set(_pyzy_srcs
  BopomofoContext.cc Database.cc DoublePinyinContext.cc DynamicSpecialPhrase.cc
  FullPinyinContext.cc InputContext.cc PhoneticContext.cc PhraseEditor.cc
  PinyinContext.cc PinyinParser.cc SimpTradConverter.cc SpecialPhraseTable.cc
  Variant.cc)
list(TRANSFORM _pyzy_srcs PREPEND "${PYZY_SRC}/src/")

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
# GCC/Clang predefine __GXX_EXPERIMENTAL_CXX0X__ in C++11 mode (it selects the
# std::shared_ptr path over boost in Util.h); MSVC does not, so define it there.
target_compile_definitions(pyzy PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:__GXX_EXPERIMENTAL_CXX0X__>)

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
  find_package(Python3 COMPONENTS Interpreter)
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
