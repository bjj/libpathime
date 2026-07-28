# Build-wide preamble and user-facing options.
# Included once from the top-level CMakeLists.

# --- Reject in-source builds: they scatter generated files through the tree. ---
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR
    "In-source builds are not allowed. Run from a separate build directory, e.g.\n"
    "  cmake -S . -B build && cmake --build build")
endif()

# --- Default build type on single-config generators (Ninja/Makefiles). ---
get_property(_lpi_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(PROJECT_IS_TOP_LEVEL AND NOT _lpi_multi_config AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
    Debug Release RelWithDebInfo MinSizeRel)
endif()

# --- Language standards. pyzy requires C++11; the C submodules build as C11. ---
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Shared libraries need PIC; also lets the codegen host tools link the libs.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# --- Parallel compilation with MSBuild ---------------------------------------
# MSBuild has two independent levels of parallelism and neither is on by
# default. `cmake --build -j` gives it the first (/m: several .vcxproj at once)
# — the build presets pass it. The second is per-project: without /MP, the
# ClCompile task hands cl.exe one source file at a time, so a 13-file library
# like anthy's src-worddic compiles on one core no matter what /m is set to.
#
# Restricted to real cl.exe on a multi-config (Visual Studio) generator. Ninja
# already schedules one cl.exe per source itself, so adding /MP there would
# oversubscribe the machine by its own job count squared; and clang-cl accepts
# the flag only to ignore it.
if(MSVC AND CMAKE_C_COMPILER_ID STREQUAL "MSVC" AND _lpi_multi_config)
  add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:/MP>")
endif()

# --- Only steer global/build-tree settings when we are the top-level project. ---
if(PROJECT_IS_TOP_LEVEL)
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib")
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib")
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
  # On Windows, put import libs next to DLLs and DLLs next to the tools that
  # load them so build-time codegen executables can find them.
  set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

include(GNUInstallDirs)

option(BUILD_SHARED_LIBS "Build submodule libraries as shared libraries" ON)

# --- Backend selection. The first three map to one vendored submodule each. ---
option(LIBPATHIME_WITH_HANGUL "Build the Korean (libhangul) backend"        ON)
option(LIBPATHIME_WITH_ANTHY  "Build the Japanese (anthy-unicode) backend"  ON)
option(LIBPATHIME_WITH_PYZY   "Build the Chinese (pyzy) backend"            ON)

# The table-driven backend is not a submodule: ibus-table is Python, so it
# cannot be wrapped, and our table engine (docs/ibus-table-mapping.md) is a peer of
# the vendored libraries rather than a wrapper around one.
#
# ON like the other three, and for the same reason they are: a default should
# describe what the library is for, not what happens to be cheapest to build.
# Pinyin alone does not reach the whole Chinese market — Cangjie, Quick, Wubi
# and Zhuyin are how a great many people actually type, and a library that
# shipped them off by default would be telling an embedder who does not read
# Chinese that they are optional. They are not.
#
# Missing dependencies still turn it off with a warning, in
# LibpathimeDependencies, exactly as they do for the other three.
option(LIBPATHIME_WITH_TABLE  "Build the table-driven (Cangjie, Wubi, Zhuyin, …) backend" ON)

# Which glyph-coverage map trims the compiled tables, or `none` to trim nothing.
#
# The filter drops table entries whose characters the target could not render,
# because the stock candidates for a partially typed Cangjie code are obscure and
# a list of tofu is worse than a short one. In practice it is "drop CJK Extension
# B and beyond": of the 40,686 distinct characters the Noto map removes from the
# five shipped tables, 40,603 are supplementary-plane.
#
# So the right answer depends on the target, and the two platforms differ enough
# that one map cannot serve both — measured against those tables:
#
#   noto      44,810 points, drops 36.6% of rows (cangjie5 52.4%)
#   windows   43,509 points, drops 38.5% of rows (cangjie5 55.1%)
#   none                     drops nothing, ~9.3 MB of tables becomes ~14.8 MB
#
# `none` is a real option on Windows rather than a footgun: a system with the
# Chinese language feature installed carries SimSun-ExtB, which covers 60,349
# supplementary code points on its own and makes every row of every shipped table
# renderable. An embedder who knows their target has it should take `none` and
# get the characters. BUILD.md, "Glyph coverage", is the guidance.
#
# Defaulted per platform rather than fixed, because a default that describes the
# wrong font landscape is a worse failure than the cross-platform difference: two
# builds of the same commit on the same platform still produce identical tables,
# and which map was used is a recorded option and a line in the build summary
# rather than a property of the machine. That is the reproducibility the
# checked-in maps exist for, and it survives.
if(WIN32)
  set(_coverage_default "windows")
else()
  set(_coverage_default "noto")
endif()
set(LIBPATHIME_TABLE_COVERAGE "${_coverage_default}" CACHE STRING
  "Glyph-coverage map trimming the compiled tables: noto, windows, or none")
set_property(CACHE LIBPATHIME_TABLE_COVERAGE PROPERTY STRINGS noto windows none)

if(NOT LIBPATHIME_TABLE_COVERAGE MATCHES "^(noto|windows|none)$")
  message(FATAL_ERROR
    "libpathime: LIBPATHIME_TABLE_COVERAGE must be noto, windows or none, "
    "not '${LIBPATHIME_TABLE_COVERAGE}'.")
endif()

# libpathime_table_coverage_definitions(<out_var>)
#
# The compile definitions that select a map in src/engines/table/coverage.cc.
# Everything compiling that file calls this — tools/table-compile, which trims
# the shipped tables, and tests/core/table_test, which asserts on the map. A
# single helper because the two disagreeing would mean testing a map the build
# does not ship.
#
# `none` still selects a map: it is honoured by passing --no-glyph-filter to the
# compile tool, not by compiling out the filter, so coverage.cc keeps a real map
# to be tested against and the test's expectations hold whatever is configured.
function(libpathime_table_coverage_definitions out_var)
  if(LIBPATHIME_TABLE_COVERAGE STREQUAL "windows")
    set(${out_var} PATHIME_TABLE_COVERAGE_WINDOWS PARENT_SCOPE)
  else()
    set(${out_var} "" PARENT_SCOPE)
  endif()
endfunction()

option(LIBPATHIME_BUILD_TESTS "Build the test suites: libpathime's own, plus each submodule's where available" OFF)

# The interactive terminal demo under demo/. Off by default, and for a
# different reason than the tests: it is not part of verifying the library, and
# it pulls in the cpp-terminal submodule, which nothing else needs.
option(LIBPATHIME_BUILD_DEMO
  "Build the interactive terminal demo (needs the demo/cpp-terminal submodule)" OFF)

# When ON, a backend whose dependencies are missing is a hard error instead of
# being silently skipped. Useful for CI that must build every backend.
option(LIBPATHIME_REQUIRE_BACKENDS
  "Fail configuration if an enabled backend's dependencies are missing" OFF)
