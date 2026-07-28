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
# cannot be wrapped, and our table engine (docs/ibus-table-spec.md) is a peer of
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
