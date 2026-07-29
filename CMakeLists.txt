# libpathime — top-level build orchestrator.
#
# Compiles the three vendored input-method submodules (libhangul,
# anthy-unicode, pyzy) and the libpathime library itself, on both Linux and
# Windows. See docs/source-layout.md for the library's layout, BUILD.md for
# how to drive this build, and docs/windows-port.md for what the Windows side
# of it is doing.
#
# Layout:
#   cmake/               helper modules + Windows compat shims
#   cmake/ports/<name>/  our native CMake ports of submodules that ship no CMake
#   engines/<name>/      the vendored submodules; libhangul is consumed via
#                        its own upstream CMake (add_subdirectory)
#   src/                 the libpathime library target
cmake_minimum_required(VERSION 3.21)

project(libpathime
  VERSION 0.1.0
  DESCRIPTION "IBus input method engine library"
  HOMEPAGE_URL "https://github.com/ben/libpathime"
  LANGUAGES C CXX)

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(LibpathimeOptions)       # build-wide preamble + LIBPATHIME_WITH_* options
include(LibpathimeCoverage)      # test-coverage instrumentation; before any target
include(LibpathimeCompat)        # libpathime_add_win32_compat() + win32 shim lib
include(LibpathimeDependencies)  # probe deps, gate backends, print the summary
include(LibpathimeRuntimeData)   # the pathime-data/ the library reads at runtime
include(LibpathimeInstall)       # the installed layout and the find_package() half

# --- Korean: libhangul ships modern, target-based CMake for its `hangul`
# library. We descend straight into that library directory rather than the
# libhangul top-level, whose CMakeLists unconditionally builds po/ (needs
# `msgfmt`, absent on stock Windows) and tools/ that we do not need. The
# library dir needs two things its (skipped) parent normally provides:
# a generated config.h at ${CMAKE_BINARY_DIR}, and the SOVERSION variables.
if(LIBPATHIME_WITH_HANGUL)
  set(ENABLE_EXTERNAL_KEYBOARDS OFF)  # avoids EXPAT + a `sed -i` codegen step
  include(CheckIncludeFile)
  check_include_file(glob.h HAVE_GLOB_H)
  configure_file("${CMAKE_CURRENT_SOURCE_DIR}/engines/libhangul/config.h.cmake.in"
                 "${CMAKE_BINARY_DIR}/config.h")
  set(LIBHANGUL_SOVERSION_MAJOR 1)  # values from engines/libhangul/CMakeLists.txt:24-26
  set(LIBHANGUL_SOVERSION_MINOR 1)
  set(LIBHANGUL_SOVERSION_PATCH 0)
  # EXCLUDE_FROM_ALL for the install rules rather than the build: it drops the
  # subdirectory from the generated install script, and upstream's rules are the
  # wrong ones for us twice over — they install to an absolute
  # CMAKE_INSTALL_FULL_LIBDIR, and on Windows they give a single bare
  # DESTINATION that puts the DLL in lib/ where the loader will never look.
  # libpathime_install_vendored() below installs the target the way
  # cmake/LibpathimeInstall.cmake lays the tree out. The `hangul` target itself
  # still builds, because libpathime links it.
  add_subdirectory(engines/libhangul/hangul EXCLUDE_FROM_ALL)
  add_library(libpathime::hangul ALIAS hangul)
  libpathime_install_vendored(hangul)
  libpathime_install_vendored_headers(hangul-1.0
    "${CMAKE_CURRENT_SOURCE_DIR}/engines/libhangul/hangul/hangul.h")
endif()

# --- Japanese: anthy-unicode has no CMake — use our native port. ---
if(LIBPATHIME_WITH_ANTHY)
  add_subdirectory(cmake/ports/anthy-unicode)
endif()

# --- Chinese: pyzy has no CMake — use our native port. ---
if(LIBPATHIME_WITH_PYZY)
  add_subdirectory(cmake/ports/pyzy)
endif()

# --- Table-driven (Wubi, Cangjie, Stroke5, Zhuyin, …): no library to build.
# ibus-table, the feature-complete reference for these methods, is Python and
# so cannot be wrapped; the engine of docs/ibus-table-mapping.md is written in
# src/engines/table/ instead, as a peer of the vendored libraries rather than a
# wrapper around one. There is nothing to add_subdirectory here: the only
# outside thing the engine needs is SQLite, gated in LibpathimeDependencies,
# and engines/ibus-table-chinese is data rather than code — its own CMake build
# needs ibus-table's Python createdb and is deliberately not used.

# --- Generated public header: include/pathime/config.h ---
# Records which backends survived the dependency gating above, so clients can
# #if them out. Placed under the binary dir's own include/pathime/ so that
# adding that one directory to a consumer's include path resolves
# <pathime/config.h> exactly as the installed layout will.
foreach(_lpi_backend HANGUL ANTHY PYZY TABLE)
  if(LIBPATHIME_WITH_${_lpi_backend})
    set(PATHIME_WITH_${_lpi_backend} 1)
  else()
    set(PATHIME_WITH_${_lpi_backend} 0)
  endif()
endforeach()
if(BUILD_SHARED_LIBS)
  set(PATHIME_BUILT_STATIC 0)
else()
  set(PATHIME_BUILT_STATIC 1)
endif()
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/pathime-config.h.in"
               "${PROJECT_BINARY_DIR}/include/pathime/config.h"
               @ONLY)
install(FILES "${PROJECT_BINARY_DIR}/include/pathime/config.h"
              "${CMAKE_CURRENT_SOURCE_DIR}/include/pathime/pathime.h"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/pathime")

# --- The library itself: the `pathime` target. It carries the public include
# directories and the PATHIME_BUILD/PATHIME_STATIC definitions, so a consumer
# that links libpathime::pathime needs no further wiring.
add_subdirectory(src)

# --- The find_package(pathime) and pkg-config halves of the install. After
# src/, which is the last directory to put anything in the export set.
libpathime_install_package()

# --- Build-time tools. Currently one: the table compiler, which turns the
# source tables of engines/ibus-table-chinese into the databases the engine
# reads. After src/, because it shares that directory's sources.
add_subdirectory(tools)

# --- The interactive demo: a client of the public API, in a terminal. All of
# its wiring — including the cpp-terminal submodule it draws with — lives under
# demo/, so nothing above this line has to know it exists. Off by default; it
# verifies nothing, it demonstrates.
if(LIBPATHIME_BUILD_DEMO)
  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/demo/cpp-terminal/CMakeLists.txt")
    message(FATAL_ERROR
      "LIBPATHIME_BUILD_DEMO is ON but demo/cpp-terminal is empty. Run\n"
      "  git submodule update --init --recursive")
  endif()
  add_subdirectory(demo)
endif()

# --- Tests. All test wiring — both the vendored suites and our own — lives
# under tests/, one directory per backend, so nothing above this line has to
# know that tests exist. Off by default: the suites need the build-time data
# (anthy.dic, android.db) and take considerably longer than the libraries.
if(LIBPATHIME_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
