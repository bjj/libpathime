# Native CMake port of anthy-unicode (Japanese kana-kanji conversion).
#
# anthy-unicode ships meson + autotools but no CMake. This file reproduces the
# meson build: the internal static libs (diclib/ordering/split), the three
# installed shared libs, and — the interesting part — the four-stage build-time
# pipeline that compiles the `anthy.dic` dictionary. Source lists are copied
# verbatim from the per-directory meson.build files.
#
# Windows collapses those six libraries into one DLL; see "One library on
# Windows" below for why, and what keeps the target names working either way.

set(ANTHY_SRC "${CMAKE_SOURCE_DIR}/engines/anthy-unicode")

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

# --- One library on Windows, upstream's six on Linux ------------------------
# anthy is packaged as three internal static libraries under three installed
# shared ones, and the pieces share *global data* across those boundaries:
#
#   * src-splitter reads `anthy_wt_all` / `anthy_wt_none`, which
#     src-worddic/wtype.c defines and anthy_init_wtypes() fills in at runtime;
#   * src-diclib owns the conf database (conf.c) and the mapped dictionary
#     (file_dic.c) that every other piece reaches through.
#
# ELF resolves all of it to one definition whatever the packaging. Windows
# resolves neither for free: a data symbol read across a DLL boundary needs
# `__declspec(dllimport)` at the *reference* to go through `__imp_`
# indirection, and a static archive pulled into two DLLs gives each one a
# private copy of that archive's state. Whether the second happens at all comes
# down to how the linker resolves an archive member that an import library also
# offers — GNU ld skips it, and a build that merely happens to link in the same
# order under link.exe would fail silently, with two conf databases.
#
# So Windows compiles the whole family as a single DLL. Every cross-piece
# reference then lands inside one module, and the process has exactly one conf
# database, one mapped dictionary and one wtype pair no matter what links it.
# The other five target names survive as interfaces onto it, so the rest of the
# tree links exactly what it always did.
#
# Set LIBPATHIME_ANTHY_SINGLE_LIBRARY to exercise either shape on either
# platform; it is how the combined build gets tested away from Windows.
if(NOT DEFINED LIBPATHIME_ANTHY_SINGLE_LIBRARY)
  set(LIBPATHIME_ANTHY_SINGLE_LIBRARY ${WIN32})
endif()

# Collect a directory's sources under a variable, relative to <dir> in the
# submodule.
# _anthy_srcs(<var> <dir> <file>...)
function(_anthy_srcs var dir)
  set(_srcs "")
  foreach(_f IN LISTS ARGN)
    list(APPEND _srcs "${ANTHY_SRC}/${dir}/${_f}")
  endforeach()
  set(${var} "${_srcs}" PARENT_SCOPE)
endfunction()

# --- the libraries ----------------------------------------------------------
# Source lists per directory, copied verbatim from the meson.build files.
_anthy_srcs(_src_diclib src-diclib
  diclib.c file_dic.c filemap.c xstr.c xchar.c alloc.c
  conf.c logger.c ruleparser.c)
_anthy_srcs(_src_ordering src-ordering
  candswap.c candsort.c commit.c relation.c infosort.c candhistory.c)
_anthy_srcs(_src_split src-splitter
  wordlist.c metaword.c depgraph.c splitter.c evalborder.c compose.c lattice.c segclass.c)
_anthy_srcs(_src_worddic src-worddic
  word_dic.c dic_util.c wtype.c texttrie.c textdict.c record.c word_lookup.c
  use_dic.c priv_dic.c mem_dic.c ext_ent.c matrix.c feature_set.c)
_anthy_srcs(_src_main src-main main.c context.c)
_anthy_srcs(_src_input src-util input.c rkconv.c rkhelper.c)

find_library(ANTHY_LIBM m)   # optional, like meson's cc.find_library('m', required:false)

if(LIBPATHIME_ANTHY_SINGLE_LIBRARY)
  add_library(anthy-unicode
    ${_src_diclib} ${_src_ordering} ${_src_split}
    ${_src_worddic} ${_src_main} ${_src_input})
  _anthy_setup(anthy-unicode)
  set_target_properties(anthy-unicode PROPERTIES VERSION 0.1.0 SOVERSION 0)
  # anthy annotates nothing for export, so the DLL needs CMake to derive its
  # .def from the objects. Set here rather than leaning on the project-wide
  # CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS, which only applies when libpathime is the
  # top-level project — this library must export whoever is building it.
  set_target_properties(anthy-unicode PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
  if(ANTHY_LIBM)
    target_link_libraries(anthy-unicode PUBLIC ${ANTHY_LIBM})
  endif()

  # Upstream's other five names, so every existing target_link_libraries() call
  # in this tree keeps resolving. They carry no objects of their own.
  foreach(_part anthy_diclib anthy_ordering anthy_split
                anthydic-unicode anthyinput-unicode)
    add_library(${_part} INTERFACE)
    target_link_libraries(${_part} INTERFACE anthy-unicode)
  endforeach()

  set(_anthy_install_targets anthy-unicode)
  # No artifacts of their own, but libpathime links two of them by name, so they
  # have to be in the export set for the generated targets file to resolve —
  # whatever the library shape. Passing them to the install helper costs nothing
  # else: an INTERFACE library installs no file.
  set(_anthy_internal_targets anthy_diclib anthy_ordering anthy_split
                              anthydic-unicode anthyinput-unicode)
  set(_anthy_internal_always TRUE)
else()
  # Internal static libraries (meson: static_library), absorbed into the
  # installed shared ones below.
  add_library(anthy_diclib   STATIC ${_src_diclib})
  add_library(anthy_ordering STATIC ${_src_ordering})
  add_library(anthy_split    STATIC ${_src_split})
  foreach(_part anthy_diclib anthy_ordering anthy_split)
    _anthy_setup(${_part})
  endforeach()
  target_link_libraries(anthy_split PUBLIC anthy_diclib)
  if(ANTHY_LIBM)
    target_link_libraries(anthy_split PUBLIC ${ANTHY_LIBM})
  endif()

  # Installed shared libraries.
  add_library(anthydic-unicode ${_src_worddic})
  _anthy_setup(anthydic-unicode)
  target_link_libraries(anthydic-unicode PRIVATE anthy_diclib)
  set_target_properties(anthydic-unicode PROPERTIES VERSION 0.1.0 SOVERSION 0)

  add_library(anthy-unicode ${_src_main})
  _anthy_setup(anthy-unicode)
  target_link_libraries(anthy-unicode PRIVATE anthydic-unicode anthy_ordering anthy_split)
  set_target_properties(anthy-unicode PROPERTIES VERSION 0.1.0 SOVERSION 0)

  add_library(anthyinput-unicode ${_src_input})
  _anthy_setup(anthyinput-unicode)
  target_link_libraries(anthyinput-unicode PRIVATE anthy-unicode anthydic-unicode)
  set_target_properties(anthyinput-unicode PROPERTIES VERSION 0.0.0 SOVERSION 0)

  # Listed dependents-first, which is the order a static link line needs and
  # the order cmake/LibpathimeInstall.cmake writes into Libs.private.
  set(_anthy_install_targets anthyinput-unicode anthy-unicode anthydic-unicode)
  # Real archives here, absorbed into the shared libraries above — so they
  # travel only when there are no shared libraries to absorb them.
  set(_anthy_internal_targets anthy_ordering anthy_split anthy_diclib)
  set(_anthy_internal_always FALSE)
endif()

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

# Published for cmake/LibpathimeRuntimeData.cmake, which puts a copy where
# libpathime looks for it, and for tests/anthy, which points anthy's own test
# programs at it.
set(ANTHY_DIC_FILE "${ANTHY_DIC_FILE}" CACHE INTERNAL
  "The anthy.dic this build produces")

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
set(ANTHY_TEST_RUNDIR   "${_dic}/test"        CACHE INTERNAL "cwd for the vendored tests")
if(LIBPATHIME_BUILD_TESTS)
  file(MAKE_DIRECTORY "${ANTHY_TEST_RUNDIR}")

  # test/main.c links upstream's convdb helper (meson: `convdb_dep`), a static
  # library over src-util/convdb.c that reads and writes the test.exp result
  # database. It deliberately reaches into private headers of src-main,
  # src-splitter and src-worddic by relative path, so it must be compiled from
  # its original location in the submodule. Nothing but the test uses it, hence
  # the gate.
  add_library(anthy_convdb STATIC "${ANTHY_SRC}/src-util/convdb.c")
  _anthy_setup(anthy_convdb)
  target_link_libraries(anthy_convdb PUBLIC anthy-unicode anthydic-unicode)
endif()

# --- install ----------------------------------------------------------------
# Into the private directory beside libpathime, and no headers unless the build
# was asked for the system-wide layout — cmake/LibpathimeInstall.cmake explains
# why our anthy is not the system's.
libpathime_install_vendored(${_anthy_install_targets})
if(_anthy_internal_always OR NOT BUILD_SHARED_LIBS)
  libpathime_install_vendored(${_anthy_internal_targets})
endif()
libpathime_install_vendored_headers(anthy-unicode-1.0/anthy
  "${ANTHY_SRC}/anthy/anthy.h" "${ANTHY_SRC}/anthy/dicutil.h" "${ANTHY_SRC}/anthy/input.h")
# anthy.dic is not installed here: libpathime tells anthy where its dictionary
# is, so the copy that matters is the one under pathime-data/ that
# cmake/LibpathimeRuntimeData.cmake installs.
