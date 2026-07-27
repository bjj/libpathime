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
  # Upstream's install() gives a single bare DESTINATION, which on Windows puts
  # the DLL in lib/ alongside its import library — where the loader will never
  # look. Add a copy in bin/ so the installed tree is actually runnable.
  if(WIN32 AND BUILD_SHARED_LIBS)
    install(FILES $<TARGET_FILE:hangul> DESTINATION ${CMAKE_INSTALL_BINDIR})
  endif()
endif()

# --- Japanese: anthy-unicode has no CMake — use our native port. ---
if(LIBPATHIME_WITH_ANTHY)
  add_subdirectory(cmake/ports/anthy-unicode)
endif()

# --- Chinese: pyzy has no CMake — use our native port. ---
if(LIBPATHIME_WITH_PYZY)
  add_subdirectory(cmake/ports/pyzy)
endif()

# --- Generated public header: include/pathime/config.h ---
# Records which backends survived the dependency gating above, so clients can
# #if them out. Placed under the binary dir's own include/pathime/ so that
# adding that one directory to a consumer's include path resolves
# <pathime/config.h> exactly as the installed layout will.
foreach(_lpi_backend HANGUL ANTHY PYZY)
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

# The library itself does not exist yet; this interface target carries the two
# include directories (source tree for pathime.h, binary tree for config.h) so
# that anything added later — or a test that just #includes the header — gets
# both without repeating the paths.
add_library(pathime_public_headers INTERFACE)
add_library(libpathime::headers ALIAS pathime_public_headers)
target_include_directories(pathime_public_headers INTERFACE
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
if(NOT BUILD_SHARED_LIBS)
  target_compile_definitions(pathime_public_headers INTERFACE PATHIME_STATIC)
endif()

# --- Tests. All test wiring — both the vendored suites and our own — lives
# under tests/, one directory per backend, so nothing above this line has to
# know that tests exist. Off by default: the suites need the build-time data
# (anthy.dic, android.db) and take considerably longer than the libraries.
if(LIBPATHIME_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
