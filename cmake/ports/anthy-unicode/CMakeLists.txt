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

# Build one library from a directory + file list, applying the common setup.
# _anthy_lib(<target> STATIC|SHARED-per-BUILD_SHARED_LIBS <dir> <file>...)
function(_anthy_lib target kind dir)
  set(_srcs ${ARGN})
  list(TRANSFORM _srcs PREPEND "${ANTHY_SRC}/${dir}/")
  if(kind STREQUAL "internal")
    add_library(${target} STATIC ${_srcs})   # always static, absorbed into the shared libs
  else()
    add_library(${target} ${_srcs})           # honours BUILD_SHARED_LIBS
  endif()
  _anthy_setup(${target})
endfunction()

# --- internal static libraries (meson: static_library) ----------------------
_anthy_lib(anthy_diclib internal src-diclib
  diclib.c file_dic.c filemap.c xstr.c xchar.c alloc.c conf.c logger.c ruleparser.c)

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
function(_anthy_tool target dir)
  set(_srcs ${ARGN})
  list(TRANSFORM _srcs PREPEND "${ANTHY_SRC}/${dir}/")
  add_executable(${target} ${_srcs})
  _anthy_setup(${target})
  target_link_libraries(${target} PRIVATE anthy-unicode anthydic-unicode)
endfunction()

_anthy_tool(anthy_mkworddic mkworddic mkdic.c writewords.c mkudic.c calcfreq.c)
_anthy_tool(anthy_calctrans calctrans calctrans.c input_set.c corpus.c)

add_executable(anthy_mkdepgraph "${ANTHY_SRC}/depgraph/mkdepgraph.c")
_anthy_setup(anthy_mkdepgraph)
target_link_libraries(anthy_mkdepgraph PRIVATE anthy-unicode anthydic-unicode)
# mkdepgraph reads its *.depword inputs from SRCDIR (compiled in), like meson.
target_compile_definitions(anthy_mkdepgraph PRIVATE
  "SRCDIR=\"${ANTHY_SRC}/depgraph\""
  "BLDDIR=\"${CMAKE_CURRENT_BINARY_DIR}/dic/depgraph\"")

add_executable(anthy_mkfiledic "${ANTHY_SRC}/mkanthydic/mkfiledic.c")
_anthy_setup(anthy_mkfiledic)
target_link_libraries(anthy_mkfiledic PRIVATE anthy_diclib)

# --- dictionary pipeline ----------------------------------------------------
# mkfiledic reads intermediates from <prefix>/{mkworddic,depgraph,calctrans}/,
# so we stage every stage's output into that exact layout under build/dic.
set(_dic "${CMAKE_CURRENT_BINARY_DIR}/dic")
file(MAKE_DIRECTORY "${_dic}/mkworddic" "${_dic}/depgraph" "${_dic}/calctrans")

# Stage 1: word dictionary. dict.args lists absolute source paths via top_srcdir.
set(top_srcdir "${ANTHY_SRC}")
configure_file("${ANTHY_SRC}/mkworddic/dict.args.in" "${_dic}/mkworddic/dict.args" @ONLY)
add_custom_command(
  OUTPUT "${_dic}/mkworddic/anthy.wdic"
  COMMAND anthy_mkworddic -f "${_dic}/mkworddic/dict.args" -o anthy.wdic
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

# Stage 3: dependency graph.
add_custom_command(
  OUTPUT "${_dic}/depgraph/anthy.dep"
  COMMAND anthy_mkdepgraph -o "${_dic}/depgraph/anthy.dep"
  DEPENDS anthy_mkdepgraph
  COMMENT "anthy: building dependency graph (anthy.dep)"
  VERBATIM)

# Stage 4: assemble anthy.dic from all of the above.
add_custom_command(
  OUTPUT "${_dic}/anthy.dic"
  COMMAND anthy_mkfiledic -p "${_dic}" -o "${_dic}/anthy.dic"
  DEPENDS anthy_mkfiledic
          "${_dic}/mkworddic/anthy.wdic"
          "${_dic}/depgraph/anthy.dep"
          "${_dic}/calctrans/anthy.cand_info"  "${_dic}/calctrans/anthy.trans_info"
          "${_dic}/calctrans/anthy.corpus_array" "${_dic}/calctrans/anthy.corpus_bucket"
          "${_dic}/calctrans/anthy.weak_words"
  COMMENT "anthy: assembling anthy.dic"
  VERBATIM)
add_custom_target(anthy-dic ALL DEPENDS "${_dic}/anthy.dic")

# --- install ----------------------------------------------------------------
install(TARGETS anthy-unicode anthydic-unicode anthyinput-unicode
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES
  "${ANTHY_SRC}/anthy/anthy.h" "${ANTHY_SRC}/anthy/dicutil.h" "${ANTHY_SRC}/anthy/input.h"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/anthy-unicode-1.0/anthy")
install(FILES "${_dic}/anthy.dic" DESTINATION "${CMAKE_INSTALL_DATADIR}/anthy-unicode")
