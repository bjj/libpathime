# Test suites: one directory per vendored backend, plus api/ for the
# libpathime API surface itself.
#
# Reached from the top-level CMakeLists only when LIBPATHIME_BUILD_TESTS is ON,
# by which point enable_testing() has been called.
#
# Two kinds of test live in the backend directories, and each holds both:
#
#   * the vendored project's own suite, wired up as CMake targets. Upstream
#     ships these under meson/autotools/its-own-CMake, none of which this build
#     descends into, so the wiring is reproduced rather than reused.
#   * tests written for libpathime, which exist mainly to hold the Windows port
#     honest. Every one of them must build and pass on Linux too — a test that
#     only runs on Windows cannot tell us whether Windows behaves like Linux.
#
# Naming: ctest names are `<backend>.<test>`, so `ctest -R '^anthy\.'` runs one
# backend's suite.

# A test needs its backend's libraries, so each directory is gated on the same
# option that builds them.
if(LIBPATHIME_WITH_HANGUL)
  add_subdirectory(hangul)
endif()

if(LIBPATHIME_WITH_ANTHY)
  add_subdirectory(anthy)
endif()

if(LIBPATHIME_WITH_PYZY)
  add_subdirectory(pyzy)
endif()

# Neither of the libpathime suites is gated: both need only core code, which
# always builds. They differ in what they link — api/ links the built library
# and sees only exported symbols, core/ compiles internal sources directly. See
# tests/core/CMakeLists.txt for why that split exists.
add_subdirectory(api)
add_subdirectory(core)
