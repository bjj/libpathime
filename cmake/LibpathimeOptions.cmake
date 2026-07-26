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

# --- Backend selection. Each maps to one vendored submodule. ---
option(LIBPATHIME_WITH_HANGUL "Build the Korean (libhangul) backend"        ON)
option(LIBPATHIME_WITH_ANTHY  "Build the Japanese (anthy-unicode) backend"  ON)
option(LIBPATHIME_WITH_PYZY   "Build the Chinese (pyzy) backend"            ON)
option(LIBPATHIME_BUILD_TESTS "Build submodule test suites where available" OFF)

# When ON, a backend whose dependencies are missing is a hard error instead of
# being silently skipped. Useful for CI that must build every backend.
option(LIBPATHIME_REQUIRE_BACKENDS
  "Fail configuration if an enabled backend's dependencies are missing" OFF)
