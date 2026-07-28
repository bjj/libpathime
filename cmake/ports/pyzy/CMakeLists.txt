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

# --- source directory (mirrored and fixed up) -------------------------------
# pyzy's sources are mirrored into the build tree and the copies are edited,
# rather than the submodule. Everything in src/ is included with quotes from
# co-located files, so the mirror resolves against itself and src/ still never
# has to go on the -I path (which would shadow <string.h> with pyzy's String.h
# on a case-insensitive filesystem).
#
# One edit is made everywhere: teaching pyzy to take its data directory from
# the embedding program (DataDir.h, and the rewrites of Database.cc and
# SpecialPhraseTable.cc below).
#
# Four more are Windows-only, because they assume GCC and/or LP64. Three of
# them no MSVC-style compiler will accept at all; the fourth compiles and then
# misbehaves at runtime.
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
#   4. BopomofoContext.cc casts a `const wchar_t *` to UCS-4. Correct only
#      where wchar_t is 32-bit; where it is 16-bit the zhuyin auxiliary text
#      silently comes out empty.
#
# The mirror is produced at configure time: editing engines/pyzy/src needs a re-run of
# cmake to take effect.
set(_pyzy_dir "${CMAKE_CURRENT_BINARY_DIR}/src")
file(GLOB _pyzy_mirror_files "${PYZY_SRC}/src/*.h" "${PYZY_SRC}/src/*.cc")
file(COPY ${_pyzy_mirror_files} DESTINATION "${_pyzy_dir}")

# DataDir.{h,cc} are ours, not upstream's. They sit in the mirror so that
# pyzy's sources can include them with quotes exactly as they include each
# other, and so that the one -I this library needs stays the one holding the
# generated <config.h>.
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/DataDir.h"
          "${CMAKE_CURRENT_SOURCE_DIR}/DataDir.cc"
     DESTINATION "${_pyzy_dir}")

# Database::open() picks the first of four candidates that exists: three
# beneath the PKGDATADIR compiled into it, then "main.db" relative to the
# process's working directory. An embedded library has no business with either
# — the first is an install prefix chosen when *this* was built, the second
# belongs to whatever program is doing the embedding — so both give way to the
# single path the program names through pyzy_set_data_dir().
#
# The g_file_test() in front of sqlite3_open_v2() is upstream's and stays:
# without it, SQLITE_OPEN_CREATE would answer a missing database by making an
# empty one, and an empty database produces no candidates and no complaint.
file(READ "${_pyzy_dir}/Database.cc" _db_cc)
string(REPLACE
[[        static const char * maindb [] = {
            PKGDATADIR"/db/local.db",
            PKGDATADIR"/db/open-phrase.db",
            PKGDATADIR"/db/android.db",
            "main.db",
        };

        size_t i;
        for (i = 0; i < G_N_ELEMENTS (maindb); i++) {
            if (!g_file_test(maindb[i], G_FILE_TEST_IS_REGULAR))
                continue;
            if (sqlite3_open_v2 (maindb[i], &m_db,
                SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) == SQLITE_OK) {
                break;
            }
        }

        if (i == G_N_ELEMENTS (maindb)) {
            g_warning ("can not open main database");
            break;
        }]]
[[        /* Rewritten by libpathime's pyzy port; see cmake/ports/pyzy. */
        const std::string maindb = dataPath ("main.db");

        if (maindb.empty () ||
            !g_file_test (maindb.c_str (), G_FILE_TEST_IS_REGULAR) ||
            sqlite3_open_v2 (maindb.c_str (), &m_db,
                SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) != SQLITE_OK) {
            g_warning ("can not open main database");
            break;
        }]]
  _db_cc_patched "${_db_cc}")
if(_db_cc_patched STREQUAL _db_cc)
  message(FATAL_ERROR
    "pyzy: could not find Database::open()'s PKGDATADIR candidate list to "
    "replace — the vendored source has changed shape.")
endif()
string(REPLACE "#include \"Config.h\""
               "#include \"Config.h\"\n#include \"DataDir.h\""
  _db_cc_patched "${_db_cc_patched}")
file(WRITE "${_pyzy_dir}/Database.cc" "${_db_cc_patched}")

# SpecialPhraseTable's constructor probes "phrases.txt" in the working
# directory, then the config directory it was given, then PKGDATADIR. The first
# and last go for the same reasons as Database's; the config directory stays,
# because that is where a client's own phrases.txt lives and it must keep
# winning over the one we ship.
file(READ "${_pyzy_dir}/SpecialPhraseTable.cc" _spt_cc)
string(REPLACE
[[    load ("phrases.txt") ||
    load (path) ||
    load (PKGDATADIR G_DIR_SEPARATOR_S "phrases.txt");]]
[[    /* Rewritten by libpathime's pyzy port; see cmake/ports/pyzy. */
    load (path) ||
    load (dataPath ("phrases.txt").c_str ());]]
  _spt_cc_patched "${_spt_cc}")
if(_spt_cc_patched STREQUAL _spt_cc)
  message(FATAL_ERROR
    "pyzy: could not find SpecialPhraseTable's phrases.txt lookup chain to "
    "replace — the vendored source has changed shape.")
endif()
string(REPLACE "#include \"DynamicSpecialPhrase.h\""
               "#include \"DataDir.h\"\n#include \"DynamicSpecialPhrase.h\""
  _spt_cc_patched "${_spt_cc_patched}")
file(WRITE "${_pyzy_dir}/SpecialPhraseTable.cc" "${_spt_cc_patched}")

if(WIN32)
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

  # 4. BopomofoContext.cc builds its auxiliary text by casting the table's
  #    `const wchar_t *` straight to `unichar *` (unsigned int) and handing it
  #    to String::operator<<, which calls g_ucs4_to_utf8. That is only ever
  #    correct where wchar_t is 32-bit UCS-4. On Windows it is 16-bit UTF-16, so
  #    two zhuyin letters are read as one code point far outside Unicode;
  #    g_ucs4_to_utf8 fails, warns, and appends nothing — the preedit's phonetic
  #    line comes up empty. Appending one code point at a time is equivalent on
  #    Linux (every zhuyin letter is in the BMP, so the UTF-8 is identical) and
  #    correct here. tests/pyzy/bopomofo_test.cc is what holds this honest.
  file(READ "${_pyzy_dir}/BopomofoContext.cc" _bpmf_cc)
  string(REPLACE
    "m_buffer << (unichar *)m_pinyin[i]->bopomofo;"
    [[/* Rewritten by libpathime's Windows port; see cmake/ports/pyzy for why.
             * The original reinterpreted a UTF-16 wchar_t string as UCS-4. */
            for (const wchar_t *bpmf_ = m_pinyin[i]->bopomofo; *bpmf_ != L'\0'; ++bpmf_)
                m_buffer.appendUnichar ((unichar) *bpmf_);]]
    _bpmf_cc_patched "${_bpmf_cc}")
  if(_bpmf_cc_patched STREQUAL _bpmf_cc)
    message(FATAL_ERROR
      "pyzy: could not find BopomofoContext.cc's `m_buffer << (unichar *)...` "
      "wchar_t-to-UCS-4 cast to correct — the vendored source has changed "
      "shape. If upstream has fixed it, drop this rewrite.")
  endif()
  file(WRITE "${_pyzy_dir}/BopomofoContext.cc" "${_bpmf_cc_patched}")
endif()

# Anything that compiles against pyzy's *private* headers has to use the same
# copies this library was built from — the mirror, never the submodule, since
# the two differ. tests/pyzy does exactly that, so publish the directory rather
# than let the test tree work it out again.
set(PYZY_EFFECTIVE_SRC_DIR "${_pyzy_dir}" CACHE INTERNAL
  "The rewritten pyzy source directory this build compiles")

# --- library (source list from src/Makefile.am:libpyzy_c_sources, plus ours) -
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
  "${PYZY_SRC}/src/Const.h" "${PYZY_SRC}/src/InputContext.h" "${PYZY_SRC}/src/Variant.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/DataDir.h"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/pyzy-1.0/PyZy")
# phrases.txt is not installed here, for the same reason android.db is not:
# libpathime tells pyzy where to look, so the copy that matters is the one
# under pathime-data/ that cmake/LibpathimeRuntimeData.cmake installs.
set(PYZY_PHRASES_FILE "${PYZY_SRC}/src/phrases.txt" CACHE INTERNAL
  "pyzy's stock special-phrase table")
