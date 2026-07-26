# libpathime — top-level build orchestrator.
#
# Right now this build exists only to compile the three vendored input-method
# submodules (libhangul, anthy-unicode, pyzy) from a single tree, on both Linux
# and Windows. The libpathime library itself is not written yet.
#
# Layout:
#   cmake/               helper modules + Windows compat shims
#   cmake/ports/<name>/  our native CMake ports of submodules that ship no CMake
#   libhangul/           consumed via its own upstream CMake (add_subdirectory)
cmake_minimum_required(VERSION 3.21)

project(libpathime
  VERSION 0.1.0
  DESCRIPTION "IBus input method engine library"
  HOMEPAGE_URL "https://github.com/ben/libpathime"
  LANGUAGES C CXX)

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(LibpathimeOptions)       # build-wide preamble + LIBPATHIME_WITH_* options
include(LibpathimeCompat)        # libpathime_add_win32_compat() + win32 shim lib
include(LibpathimeDependencies)  # probe deps, gate backends, print the summary

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
  configure_file("${CMAKE_CURRENT_SOURCE_DIR}/libhangul/config.h.cmake.in"
                 "${CMAKE_BINARY_DIR}/config.h")
  set(LIBHANGUL_SOVERSION_MAJOR 1)  # values from libhangul/CMakeLists.txt:24-26
  set(LIBHANGUL_SOVERSION_MINOR 1)
  set(LIBHANGUL_SOVERSION_PATCH 0)
  add_subdirectory(libhangul/hangul)
  add_library(libpathime::hangul ALIAS hangul)
endif()

# --- Japanese: anthy-unicode has no CMake — use our native port. ---
if(LIBPATHIME_WITH_ANTHY)
  add_subdirectory(cmake/ports/anthy-unicode)
endif()

# --- Chinese: pyzy has no CMake — use our native port. ---
if(LIBPATHIME_WITH_PYZY)
  add_subdirectory(cmake/ports/pyzy)
endif()
