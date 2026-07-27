# Native CMake port of anthy-unicode (Japanese kana-kanji conversion).
#
# anthy-unicode ships meson + autotools but no CMake. This file reproduces the
# meson build: the internal static libs (diclib/ordering/split), the three
# installed shared libs, and — the interesting part — the four-stage build-time
# pipeline that compiles the `anthy.dic` dictionary. Source lists are copied
# verbatim from the per-directory meson.build files.

set(ANTHY_SRC "${CMAKE_SOURCE_DIR}/anthy-unicode")

# --- config.h ---------------------------------------------------------------
include(CheckFunctionExists)
check_function_exists(strerror_r HAVE_STRERROR_R)
set(ANTHY_PACKAGE "anthy-unicode")
set(ANTHY_VERSION "1.0.0")   # base version from configure.ac (AC_INIT); no datestamp needed to build
configure_file(config.h.in "${CMAKE_CURRENT_BINARY_DIR}/config.h" @ONLY)

# Every anthy TU includes <config.h> and <anthy/...>; CONF_DIR is a -D like meson's.
set(_anthy_inc "${ANTHY_SRC}" "${CMAKE_CURRENT_BINARY_DIR}")

# Apply the include dirs, CONF_DIR define, and (on Windows) the POSIX shims.
function(_anthy_setup tgt)
  target_include_directories(${tgt} PRIVATE ${_anthy_inc})
  target_compile_definitions(${tgt} PRIVATE
    "CONF_DIR=\"${CMAKE_INSTALL_FULL_SYSCONFDIR}\"")
  libpathime_add_win32_compat(${tgt})
endfunction()

# On Windows the anthy family is always static, whatever BUILD_SHARED_LIBS says.
# anthy's libraries share *global data* across the library boundary: the
# splitter (src-splitter) reads `anthy_wt_all` / `anthy_wt_none`, which
# src-worddic/wtype.c defines and anthy_init_wtypes() fills in at runtime.
# ELF resolves that transparently; Windows cannot. A DLL data export is only
# reachable through `__imp_`-prefixed indirection, which the consumer gets by
# declaring the symbol `__declspec(dllimport)` — an annotation that would have
# to go into the vendored <anthy/wtype.h>, and patching the submodules is
# exactly what this port avoids. Duplicating wtype.c per DLL is not an option
# either: each copy would need its own initialisation. Building the family
# static sidesteps the whole question, and has the side benefit of making the
# build-time codegen tools self-contained (no DLL to locate when they run).
#
# The cost, stated rather than discovered twice: a shared libpathime absorbs a
# private copy of anthy, and anthy's configuration and initialised-once flags
# are process-global state (the conf database in src-diclib/conf.c, is_init_ok
# in src-main/main.c, dic_init_count in src-worddic/word_dic.c). So on Windows
# any *other* module in the process that links anthy gets a second, independent
# anthy: its anthy_conf_override() calls are invisible to libpathime's copy and
# vice versa. ELF hides this by resolving both to one definition, which is why
# it can pass on Linux and fail on Windows with no source difference at all.
# Consequence for us: nothing that links libpathime may also link anthy —
# tests/api/CMakeLists.txt is where that bit, and the way around it.
set(_anthy_public_kind "")   # empty => honour BUILD_SHARED_LIBS
if(WIN32)
  set(_anthy_public_kind STATIC)
endif()

# Build one library from a directory + file list, applying the common setup.
# _anthy_lib(<target> internal|public <dir> <file>...)
# File names are relative to <dir> in the submodule; an absolute path is taken
# as-is, which is how the generated Windows variants below get substituted in.
function(_anthy_lib target kind dir)
  set(_srcs "")
  foreach(_f IN LISTS ARGN)
    if(IS_ABSOLUTE "${_f}")
      list(APPEND _srcs "${_f}")
    else()
      list(APPEND _srcs "${ANTHY_SRC}/${dir}/${_f}")
    endif()
  endforeach()
  if(kind STREQUAL "internal")
    add_library(${target} STATIC ${_srcs})   # always static, absorbed into the shared libs
  else()
    add_library(${target} ${_anthy_public_kind} ${_srcs})
  endif()
  _anthy_setup(${target})
endfunction()

# --- LLP64 fix for the slab allocator ---------------------------------------
# src-diclib/alloc.c round-trips heap pointers through `unsigned long` to do
# header arithmetic: `(struct chunk *)((unsigned long)s - CHUNK_HEADER_SIZE)`,
# and the same for the page-ownership search in anthy_sfree(). Under LP64 that
# is lossless. Under Windows' LLP64 `unsigned long` is 32 bits, so every
# allocation above the 4 GB line — which is to say all of them — has its high
# half discarded and anthy_sfree() either abort()s on the page-magic check or
# frees the wrong object. Compile a copy with the casts widened to uintptr_t;
# every `unsigned long` in the file is one of these pointer casts.
set(_anthy_alloc_c alloc.c)
if(WIN32)
  file(READ "${ANTHY_SRC}/src-diclib/alloc.c" _alloc_c)
  string(REPLACE "(unsigned long)" "(uintptr_t)" _alloc_c_fixed "${_alloc_c}")
  if(_alloc_c_fixed STREQUAL _alloc_c)
    message(FATAL_ERROR
      "anthy: src-diclib/alloc.c has no `(unsigned long)` pointer casts to "
      "widen — the vendored source has changed shape.")
  endif()
  string(REPLACE "#include <stdio.h>" "#include <stdint.h>\n#include <stdio.h>"
                 _alloc_c_fixed "${_alloc_c_fixed}")
  set(_anthy_alloc_c "${CMAKE_CURRENT_BINARY_DIR}/src-win32/alloc.c")
  file(WRITE "${_anthy_alloc_c}"
    "/* Generated by libpathime's anthy port from ${ANTHY_SRC}/src-diclib/alloc.c:\n"
    " * pointer casts widened from unsigned long to uintptr_t for LLP64.\n"
    " * Do not edit; edit the submodule and re-run cmake. */\n"
    "${_alloc_c_fixed}")
endif()

# --- Windows path handling for the profile directory ------------------------
# src-diclib/file_dic.c carries a copy of glib's g_mkdir_with_parents() and the
# two path helpers it leans on, and all three assume POSIX paths:
#
#   * ANTHY_IS_DIR_SEPARATOR() (diclib_inner.h) recognises only '/', but the
#     home directory anthy gets on Windows comes from %USERPROFILE% via the
#     getpwuid() shim — "C:\Users\someone", with no forward slash in it at all.
#   * anthy_path_is_absolute() calls a path absolute only if it *starts* with a
#     separator, so "C:/Users/..." is treated as relative and the parent walk
#     begins at the first component, "C:". stat("C:") fails on Windows — a bare
#     drive letter is not a path — so the code concludes that the component
#     exists but is not a directory and gives up with ENOTDIR.
#
# Either way anthy_check_user_dir() cannot create $HOME/.config/anthy, and the
# only trace is one line on the logger:
#
#     Failed to create profile directory: Not a directory
#
# after which the personal dictionary, the conversion history and prediction
# are all silently unavailable — anthy keeps converting, so nothing looks
# broken. tests/anthy/ covers this from three directions (anthy.dicutil,
# anthy.segment, and upstream's checklib).
#
# The copy teaches the separator macro about '\' and both helpers about drive
# prefixes. A drive prefix only counts when a separator follows it, so
# is_absolute() and skip_root() agree: skip_root() must never return NULL for
# a path is_absolute() accepted, or the walk dereferences it.
set(_anthy_file_dic_c file_dic.c)
if(WIN32)
  file(READ "${ANTHY_SRC}/src-diclib/file_dic.c" _file_dic_c)
  set(_fd "${_file_dic_c}")

  set(_fd_prologue
"#ifdef _WIN32
#undef ANTHY_IS_DIR_SEPARATOR
#define ANTHY_IS_DIR_SEPARATOR(c) ((c) == '/' || (c) == '\\\\')
#define ANTHY_HAS_DRIVE_PREFIX(p) ((p)[0] && (p)[1] == ':' && ANTHY_IS_DIR_SEPARATOR ((p)[2]))
#else
#define ANTHY_HAS_DRIVE_PREFIX(p) 0
#endif

static int
anthy_path_is_absolute (const char *file_name)")
  string(REPLACE
    "static int\nanthy_path_is_absolute (const char *file_name)"
    "${_fd_prologue}"
    _fd "${_fd}")

  string(REPLACE
    "  if (ANTHY_IS_DIR_SEPARATOR (file_name[0]))\n    return TRUE;\n"
    "  if (ANTHY_IS_DIR_SEPARATOR (file_name[0]))\n    return TRUE;\n\n  if (ANTHY_HAS_DRIVE_PREFIX (file_name))\n    return TRUE;\n"
    _fd "${_fd}")

  string(REPLACE
    "  if (ANTHY_IS_DIR_SEPARATOR (file_name[0]))\n    {\n"
    "  if (ANTHY_HAS_DRIVE_PREFIX (file_name))\n    file_name += 2;\n  if (ANTHY_IS_DIR_SEPARATOR (file_name[0]))\n    {\n"
    _fd "${_fd}")

  # The copy lives in the build tree, so its one quote-form include no longer
  # resolves relative to the source file. Absolutise it rather than putting
  # src-diclib/ on the include path, which would expose its private headers to
  # every other translation unit in the library.
  string(REPLACE
    "#include \"diclib_inner.h\""
    "#include \"${ANTHY_SRC}/src-diclib/diclib_inner.h\""
    _fd "${_fd}")

  # Four edits, four chances for the vendored source to have moved on. Rather
  # than diff the whole file, check that each one actually landed — a
  # string(REPLACE) whose pattern no longer matches is silently a no-op, and
  # this fix failing quietly would look exactly like the bug it removes.
  foreach(_marker "#undef ANTHY_IS_DIR_SEPARATOR"
                  "return TRUE;\n\n  if (ANTHY_HAS_DRIVE_PREFIX (file_name))"
                  "if (ANTHY_HAS_DRIVE_PREFIX (file_name))\n    file_name += 2;"
                  "#include \"${ANTHY_SRC}/src-diclib/diclib_inner.h\"")
    string(FIND "${_fd}" "${_marker}" _found)
    if(_found EQUAL -1)
      message(FATAL_ERROR
        "anthy: could not apply the Windows path fix to src-diclib/file_dic.c "
        "— the vendored source has changed shape around\n  ${_marker}")
    endif()
  endforeach()

  set(_anthy_file_dic_c "${CMAKE_CURRENT_BINARY_DIR}/src-win32/file_dic.c")
  file(WRITE "${_anthy_file_dic_c}"
    "/* Generated by libpathime's anthy port from ${ANTHY_SRC}/src-diclib/file_dic.c:\n"
    " * the POSIX-only path walk taught about Windows drive letters and '\\\\'.\n"
    " * Do not edit; edit the submodule and re-run cmake. */\n"
    "${_fd}")
endif()

# --- internal static libraries (meson: static_library) ----------------------
_anthy_lib(anthy_diclib internal src-diclib
  diclib.c ${_anthy_file_dic_c} filemap.c xstr.c xchar.c ${_anthy_alloc_c}
  conf.c logger.c ruleparser.c)

_anthy_lib(anthy_ordering internal src-ordering
  candswap.c candsort.c commit.c relation.c infosort.c candhistory.c)

_anthy_lib(anthy_split internal src-splitter
  wordlist.c metaword.c depgraph.c splitter.c evalborder.c compose.c lattice.c segclass.c)
target_link_libraries(anthy_split PUBLIC anthy_diclib)
find_library(ANTHY_LIBM m)   # optional, like meson's cc.find_library('m', required:false)
if(ANTHY_LIBM)
  target_link_libraries(anthy_split PUBLIC ${ANTHY_LIBM})
endif()

# --- installed shared libraries ---------------------------------------------
_anthy_lib(anthydic-unicode public src-worddic
  word_dic.c dic_util.c wtype.c texttrie.c textdict.c record.c word_lookup.c
  use_dic.c priv_dic.c mem_dic.c ext_ent.c matrix.c feature_set.c)
target_link_libraries(anthydic-unicode PRIVATE anthy_diclib)
set_target_properties(anthydic-unicode PROPERTIES VERSION 0.1.0 SOVERSION 0)

_anthy_lib(anthy-unicode public src-main main.c context.c)
target_link_libraries(anthy-unicode PRIVATE anthydic-unicode anthy_ordering anthy_split)
set_target_properties(anthy-unicode PROPERTIES VERSION 0.1.0 SOVERSION 0)

_anthy_lib(anthyinput-unicode public src-util input.c rkconv.c rkhelper.c)
target_link_libraries(anthyinput-unicode PRIVATE anthy-unicode anthydic-unicode)
set_target_properties(anthyinput-unicode PROPERTIES VERSION 0.0.0 SOVERSION 0)

add_library(libpathime::anthy ALIAS anthy-unicode)

# --- codegen host tools (run during the build to produce the dictionary) ----
# These must execute on the build host, so cross-compiling is unsupported (the
# top-level dependency gate already errors out in that case).

# Windows-only fixes that every codegen tool needs:
#
#  * Stack size. The tools call alloca() inside loops (mkdic.c's line reader,
#    mkdepgraph.c's token splitter), so their peak stack scales with the
#    dictionary source they chew through. Linux gives them 8 MB by default; the
#    MSVC default is 1 MB, which mkworddic overflows on the full word list.
#  * Binary file mode. The tools fopen() their binary intermediates in text
#    mode; see cmake/compat/win32/binmode_win.c.
function(_anthy_tool_win32 target)
  if(WIN32)
    target_sources(${target} PRIVATE
      "${LIBPATHIME_COMPAT_WIN32_DIR}/binmode_win.c")
  endif()
  if(MSVC)
    target_link_options(${target} PRIVATE /STACK:8388608)
  endif()
endfunction()

function(_anthy_tool target dir)
  set(_srcs ${ARGN})
  list(TRANSFORM _srcs PREPEND "${ANTHY_SRC}/${dir}/")
  add_executable(${target} ${_srcs})
  _anthy_setup(${target})
  _anthy_tool_win32(${target})
  target_link_libraries(${target} PRIVATE anthy-unicode anthydic-unicode)
endfunction()

_anthy_tool(anthy_mkworddic mkworddic mkdic.c writewords.c mkudic.c calcfreq.c)
_anthy_tool(anthy_calctrans calctrans calctrans.c input_set.c corpus.c)

add_executable(anthy_mkdepgraph "${ANTHY_SRC}/depgraph/mkdepgraph.c")
_anthy_setup(anthy_mkdepgraph)
_anthy_tool_win32(anthy_mkdepgraph)
target_link_libraries(anthy_mkdepgraph PRIVATE anthy-unicode anthydic-unicode)
# mkdepgraph reads its *.depword inputs from SRCDIR (compiled in), like meson.
target_compile_definitions(anthy_mkdepgraph PRIVATE
  "SRCDIR=\"${ANTHY_SRC}/depgraph\""
  "BLDDIR=\"${CMAKE_CURRENT_BINARY_DIR}/dic/depgraph\"")

add_executable(anthy_mkfiledic "${ANTHY_SRC}/mkanthydic/mkfiledic.c")
_anthy_setup(anthy_mkfiledic)
_anthy_tool_win32(anthy_mkfiledic)
target_link_libraries(anthy_mkfiledic PRIVATE anthy_diclib)

# --- dictionary pipeline ----------------------------------------------------
# mkfiledic reads intermediates from <prefix>/{mkworddic,depgraph,calctrans}/,
# so we stage every stage's output into that exact layout under build/dic.
#
# The finished anthy.dic goes in mkanthydic/ for the same reason: that is where
# upstream's meson build leaves it, and the vendored test programs hard-code
# `../mkanthydic/anthy.dic` relative to their working directory (see
# test/check.c:16 and friends). Keeping the layout identical to upstream's lets
# tests/anthy/ point those programs at this tree by choosing a working
# directory, rather than by copying a 20 MB dictionary around.
set(_dic "${CMAKE_CURRENT_BINARY_DIR}/dic")
file(MAKE_DIRECTORY "${_dic}/mkworddic" "${_dic}/depgraph" "${_dic}/calctrans"
                    "${_dic}/mkanthydic")

# mkworddic writes its section files to $TMPDIR, falling back to a hard-coded
# "/tmp" that does not exist on Windows. Point it at the build tree everywhere
# so the build never depends on the host's temp directory.
set(_dic_tmp "${CMAKE_CURRENT_BINARY_DIR}/tmp")
file(MAKE_DIRECTORY "${_dic_tmp}")
set(_with_tmpdir ${CMAKE_COMMAND} -E env "TMPDIR=${_dic_tmp}")

# anthy-unicode.conf tells mkdepgraph which grammar files to read (INDEPWORD /
# DEPWORD). mkdepgraph hard-codes its location as BLDDIR/../anthy-unicode.conf,
# which is why BLDDIR points one level below _dic. Without this file mkdepgraph
# still exits 0 but emits a 12-byte, ruleless anthy.dep — a silent failure, so
# stage 3 below asserts on the output size.
#
# NEWLINE_STYLE UNIX on this and dict.args below is load-bearing on Windows:
# the codegen tools run with the CRT in binary mode (binmode_win.c), so anthy's
# LF-oriented line parser would keep the CR of a CRLF file as part of the last
# token — turning a path into one that cannot be opened. Every text file anthy
# reads is LF, and these generated ones must match.
set(prefix "${CMAKE_INSTALL_PREFIX}")
set(datadir "${CMAKE_INSTALL_FULL_DATADIR}")
set(PACKAGE "${ANTHY_PACKAGE}")
configure_file("${ANTHY_SRC}/anthy-unicode.conf.in" "${_dic}/anthy-unicode.conf"
               @ONLY NEWLINE_STYLE UNIX)

# Stage 1: word dictionary. dict.args lists absolute source paths via top_srcdir.
#
# The `-f` path is passed as `./dict.args`, not absolutely: anthy's
# ruleparser.c only recognises a path as "already absolute" when it starts with
# "/" or "./", so a Windows "C:\..." path gets ANTHYDIR prepended and the open
# fails. Relative-to-WORKING_DIRECTORY is portable. (The *contents* of
# dict.args are read with plain fopen(), so absolute paths are fine there.)
set(top_srcdir "${ANTHY_SRC}")
configure_file("${ANTHY_SRC}/mkworddic/dict.args.in" "${_dic}/mkworddic/dict.args"
               @ONLY NEWLINE_STYLE UNIX)
add_custom_command(
  OUTPUT "${_dic}/mkworddic/anthy.wdic"
  COMMAND ${_with_tmpdir} $<TARGET_FILE:anthy_mkworddic> -f ./dict.args -o anthy.wdic
  WORKING_DIRECTORY "${_dic}/mkworddic"
  DEPENDS anthy_mkworddic "${_dic}/mkworddic/dict.args"
  COMMENT "anthy: building word dictionary (anthy.wdic)"
  VERBATIM)

# Stage 2: transition matrices from the pre-built corpus.
add_custom_command(
  OUTPUT "${_dic}/calctrans/anthy.cand_info"   "${_dic}/calctrans/anthy.trans_info"
         "${_dic}/calctrans/anthy.corpus_array" "${_dic}/calctrans/anthy.corpus_bucket"
         "${_dic}/calctrans/anthy.weak_words"
  COMMAND anthy_calctrans -c "${ANTHY_SRC}/calctrans/corpus_info"
                             "${ANTHY_SRC}/calctrans/weak_words" -d "${_dic}/calctrans"
  DEPENDS anthy_calctrans "${ANTHY_SRC}/calctrans/corpus_info" "${ANTHY_SRC}/calctrans/weak_words"
  COMMENT "anthy: computing transition matrices"
  VERBATIM)

# Stage 3: dependency graph. mkdepgraph takes its inputs from the generated
# anthy-unicode.conf, which it locates relative to its compiled-in BLDDIR.
set(_assert_size "${CMAKE_CURRENT_SOURCE_DIR}/assert_min_size.cmake")
add_custom_command(
  OUTPUT "${_dic}/depgraph/anthy.dep"
  COMMAND anthy_mkdepgraph -o "${_dic}/depgraph/anthy.dep"
  COMMAND ${CMAKE_COMMAND} -DFILE=${_dic}/depgraph/anthy.dep -DMIN_BYTES=32768
          -DWHAT=the\ dependency\ graph -P "${_assert_size}"
  DEPENDS anthy_mkdepgraph "${_dic}/anthy-unicode.conf"
          "${ANTHY_SRC}/depgraph/master.depword" "${ANTHY_SRC}/depgraph/indepword.txt"
  COMMENT "anthy: building dependency graph (anthy.dep)"
  VERBATIM)

# Stage 4: assemble anthy.dic from all of the above.
set(ANTHY_DIC_FILE "${_dic}/mkanthydic/anthy.dic")
add_custom_command(
  OUTPUT "${ANTHY_DIC_FILE}"
  COMMAND anthy_mkfiledic -p "${_dic}" -o "${ANTHY_DIC_FILE}"
  COMMAND ${CMAKE_COMMAND} -DFILE=${ANTHY_DIC_FILE} -DMIN_BYTES=16000000
          -DWHAT=the\ dictionary -P "${_assert_size}"
  DEPENDS anthy_mkfiledic
          "${_dic}/mkworddic/anthy.wdic"
          "${_dic}/depgraph/anthy.dep"
          "${_dic}/calctrans/anthy.cand_info"  "${_dic}/calctrans/anthy.trans_info"
          "${_dic}/calctrans/anthy.corpus_array" "${_dic}/calctrans/anthy.corpus_bucket"
          "${_dic}/calctrans/anthy.weak_words"
  COMMENT "anthy: assembling anthy.dic"
  VERBATIM)
add_custom_target(anthy-dic ALL DEPENDS "${ANTHY_DIC_FILE}")

# --- test support -----------------------------------------------------------
# tests/anthy/ builds both the vendored test programs and our own. It is a
# sibling directory scope, so everything it needs from here is published as a
# cache variable rather than reached for by path.
#
# ANTHY_TEST_RUNDIR is the working directory the vendored programs must run in:
# they call anthy_conf_override() with paths written relative to the meson
# build tree's test/ directory ("../anthy-unicode.conf",
# "../mkanthydic/anthy.dic"), so the only thing that makes them resolve is
# standing in a directory whose parent is this dictionary tree. Nothing is
# written to it — the programs' output paths are all driven by -DSRCDIR /
# -DTEST_HOME, which tests/anthy/ points into its own build directory.
set(ANTHY_TEST_SRC_DIR  "${ANTHY_SRC}"        CACHE INTERNAL "anthy submodule root")
set(ANTHY_TEST_CONFIG_DIR "${CMAKE_CURRENT_BINARY_DIR}" CACHE INTERNAL "dir holding the generated config.h")
set(ANTHY_TEST_DIC_FILE "${ANTHY_DIC_FILE}"   CACHE INTERNAL "built anthy.dic")
set(ANTHY_TEST_CONF_FILE "${_dic}/anthy-unicode.conf" CACHE INTERNAL "conf for the build tree")
set(ANTHY_TEST_RUNDIR   "${_dic}/test"        CACHE INTERNAL "cwd for the vendored tests")
if(LIBPATHIME_BUILD_TESTS)
  file(MAKE_DIRECTORY "${ANTHY_TEST_RUNDIR}")

  # test/main.c links upstream's convdb helper (meson: `convdb_dep`), a static
  # library over src-util/convdb.c that reads and writes the test.exp result
  # database. It deliberately reaches into private headers of src-main,
  # src-splitter and src-worddic by relative path, so it must be compiled from
  # its original location in the submodule. Nothing but the test uses it, hence
  # the gate.
  _anthy_lib(anthy_convdb internal src-util convdb.c)
  target_link_libraries(anthy_convdb PUBLIC anthy-unicode anthydic-unicode)
endif()

# --- install ----------------------------------------------------------------
install(TARGETS anthy-unicode anthydic-unicode anthyinput-unicode
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES
  "${ANTHY_SRC}/anthy/anthy.h" "${ANTHY_SRC}/anthy/dicutil.h" "${ANTHY_SRC}/anthy/input.h"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/anthy-unicode-1.0/anthy")
install(FILES "${ANTHY_DIC_FILE}" DESTINATION "${CMAKE_INSTALL_DATADIR}/anthy-unicode")
