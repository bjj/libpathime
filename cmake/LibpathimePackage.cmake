# What `cpack` makes of the install components, and nothing else — the install
# rules themselves are cmake/LibpathimeInstall.cmake's.
#
# Two archives ship per platform-architecture pair, selected from the four
# components by which ones a cpack invocation names:
#
#   the library package  —  runtime;devel;data   (the default below)
#   the demo package     —  runtime;demo;data
#
#   cpack --config build/<preset>/CPackConfig.cmake
#   cpack --config build/<preset>/CPackConfig.cmake \
#         -D CPACK_COMPONENTS_ALL="runtime;demo;data" \
#         -D CPACK_PACKAGE_FILE_NAME=pathime-demo-<ver>-<os>-<arch>
#
# ALL_COMPONENTS_IN_ONE because each archive is a complete, runnable tree, not
# a set of per-component sub-archives; ARCHIVE_COMPONENT_INSTALL because
# without it cpack ignores the component selection entirely and packages the
# whole install. `data` rides in both archives — under IGNORE grouping a
# component lands in exactly one archive, which is why the packages are two
# cpack runs over one configuration rather than one run with groups.
#
# Guarded to the top-level project: a tree pulling libpathime in with
# add_subdirectory owns its own packaging.

if(NOT PROJECT_IS_TOP_LEVEL)
  return()
endif()

set(CPACK_PACKAGE_NAME "libpathime")
set(CPACK_PACKAGE_VENDOR "libpathime")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")

if(WIN32)
  set(CPACK_GENERATOR ZIP)
else()
  set(CPACK_GENERATOR TGZ)
endif()

set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
set(CPACK_COMPONENTS_GROUPING ALL_COMPONENTS_IN_ONE)
set(CPACK_COMPONENTS_ALL runtime devel data)

# Every archive wraps its contents in a top-level directory matching the
# archive's file name (the component-install default is flat), so extraction
# cannot scatter bin/ lib/ include/ share/ into whatever directory tar runs
# in; a prefix-style extraction is still one --strip-components=1 away. The
# source tarball already does the same via `git archive --prefix`.
set(CPACK_COMPONENT_INCLUDE_TOPLEVEL_DIRECTORY ON)

# libpathime-<ver>-<os>-<arch>, in the release artifacts' spelling: linux,
# windows, macos; x86_64/aarch64 as the toolchain reports them, with Windows'
# AMD64 folded to the x64 every Windows artifact calls itself.
if(APPLE)
  set(_lpp_os "macos")
else()
  string(TOLOWER "${CMAKE_SYSTEM_NAME}" _lpp_os)
endif()
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _lpp_arch)
if(_lpp_arch STREQUAL "amd64")
  set(_lpp_arch "x64")
endif()
set(CPACK_PACKAGE_FILE_NAME
    "libpathime-${PROJECT_VERSION}-${_lpp_os}-${_lpp_arch}")
set(LIBPATHIME_PACKAGE_SUFFIX "${PROJECT_VERSION}-${_lpp_os}-${_lpp_arch}"
    CACHE INTERNAL "the <ver>-<os>-<arch> tail both package names share")

include(CPack)
