# What an installed libpathime looks like, and what a consumer finds when they
# go looking for it.
#
# The layout follows from what the library is for. libpathime is shipped *with*
# an input-method engine rather than installed as a system component, and the
# vendored backends are why it has to be: anthy-unicode and pyzy carry
# portability commits on their own `libpathime` branches, and this build
# compiles all three with its own options and its own compat layer. A
# distribution's libhangul, libanthy-unicode or libpyzy is a different library
# wearing the same SONAME, so installing ours into the system library and
# include directories would collide with those packages outright — and an
# engine that got the distribution's copy instead of this one would be running
# code this tree has never tested.
#
# So the default install publishes one library, one set of headers, one data
# directory, and the two files that let a consumer find them:
#
#   <prefix>/include/pathime/{pathime.h,config.h}
#   <prefix>/lib/libpathime.so.0                  (bin/pathime.dll on Windows)
#   <prefix>/lib/pathime/libhangul.so.1           private: on no search path
#   <prefix>/lib/pathime/libanthy-unicode.so.0
#   <prefix>/lib/pathime/libpyzy-1.0.so.1
#   <prefix>/lib/pathime-data/...
#   <prefix>/lib/cmake/pathime/*.cmake
#   <prefix>/lib/pkgconfig/pathime.pc
#
# The vendored libraries keep their own names and SONAMEs but live in a
# directory nothing else searches; libpathime reaches them through an RPATH of
# `$ORIGIN/pathime` (`@loader_path/pathime` on macOS). No vendored header is
# installed at all, because nothing outside this build compiles against one —
# BUILD.md, "Consuming the library", is the promise this keeps. Windows has no
# RPATH, so there the vendored DLLs sit beside `pathime.dll` in `bin/`: the same
# arrangement reached by the other platform's means, since the directory of the
# loading module is what both mechanisms name.
#
# They stay separate shared libraries rather than being absorbed into
# libpathime, and that is a licensing decision as much as a layout one. All
# three are LGPL-2.1 (THIRD-PARTY.md): linked as they are here, an application
# shipping libpathime satisfies the relinking clause by the fact of their being
# replaceable files. Folding them into libpathime.so would move that obligation
# onto every embedder.
#
# LIBPATHIME_INSTALL_VENDORED=ON puts the libraries and their headers in the
# ordinary system places instead, for a packager who intends exactly the
# system-wide install this default avoids and has decided to deal with the
# consequences. Nothing in the build depends on which way it is set.

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

if(LIBPATHIME_INSTALL_VENDORED)
  set(LIBPATHIME_VENDORED_LIBDIR "${CMAKE_INSTALL_LIBDIR}")
else()
  set(LIBPATHIME_VENDORED_LIBDIR "${CMAKE_INSTALL_LIBDIR}/pathime")
endif()

# The runtime directory is the same either way: a DLL is found beside the module
# that loads it, so a vendored DLL has nowhere else it could go.
set(LIBPATHIME_VENDORED_BINDIR "${CMAKE_INSTALL_BINDIR}")

# Where pathime-data lands, and the single place that decides it: beside the
# installed libpathime, because that is what makes the default resource_dir
# correct for an installed tree. cmake/LibpathimeRuntimeData.cmake fills the
# directory; this is the arithmetic about where it is, kept here with the rest
# of the layout so that the CMake package and the .pc file can name it too.
if(WIN32 AND BUILD_SHARED_LIBS)
  set(LIBPATHIME_INSTALL_DATADIR "${CMAKE_INSTALL_BINDIR}/${LIBPATHIME_RUNTIME_DATA_DIRNAME}")
else()
  set(LIBPATHIME_INSTALL_DATADIR "${CMAKE_INSTALL_LIBDIR}/${LIBPATHIME_RUNTIME_DATA_DIRNAME}")
endif()
set(LIBPATHIME_INSTALL_FULL_DATADIR "${CMAKE_INSTALL_PREFIX}/${LIBPATHIME_INSTALL_DATADIR}")

# The one export set. It holds libpathime and the vendored libraries, in both
# link modes but for different reasons: a static build puts the vendored
# archives on the consumer's link line, and a shared one records them as
# libpathime.so's IMPORTED_LINK_DEPENDENT_LIBRARIES, which is how a consumer's
# build works out that it has an RPATH to honour. CMake requires them in the set
# either way, and what it wants recorded is true — so the generated
# pathime-targets.cmake names them. They are still not part of the interface:
# nothing outside this build has a header to compile against them with, and
# BUILD.md says so where a consumer will read it.
set(LIBPATHIME_EXPORT_SET pathimeTargets)

# libpathime_set_install_rpath(<target>)
#
# Point an installed binary at its siblings and at the private vendored
# directory. Set per target rather than through CMAKE_INSTALL_RPATH so that a
# tree which pulls libpathime in with add_subdirectory keeps its own RPATH
# policy for its own targets.
function(libpathime_set_install_rpath tgt)
  if(WIN32)
    return()   # no RPATH; see the header comment
  endif()
  if(APPLE)
    set(_origin "@loader_path")
  else()
    set(_origin "$ORIGIN")
  endif()
  set_property(TARGET ${tgt} APPEND PROPERTY INSTALL_RPATH
    "${_origin}" "${_origin}/pathime")
endfunction()

# libpathime_install_vendored(<targets>...)
#
# Install vendored backend libraries into the private directory, and record them
# for the pkg-config file. Called by each port for its own targets, and by the
# top-level CMakeLists for libhangul — whose upstream CMakeLists installs to an
# absolute CMAKE_INSTALL_FULL_LIBDIR and is not ours to change, which is why the
# top level descends into it with EXCLUDE_FROM_ALL and installs the target here.
#
# The ARCHIVE destination matters only on Windows in a shared build, where it
# catches the import library. That lands in the private directory too: it is as
# private as the DLL it names, and a consumer has no business linking it.
function(libpathime_install_vendored)
  foreach(_tgt ${ARGN})
    set_property(GLOBAL APPEND PROPERTY LIBPATHIME_VENDORED_TARGETS ${_tgt})
    libpathime_set_install_rpath(${_tgt})
    install(TARGETS ${_tgt} EXPORT ${LIBPATHIME_EXPORT_SET}
      RUNTIME DESTINATION "${LIBPATHIME_VENDORED_BINDIR}"
      LIBRARY DESTINATION "${LIBPATHIME_VENDORED_LIBDIR}"
      ARCHIVE DESTINATION "${LIBPATHIME_VENDORED_LIBDIR}")
  endforeach()
endfunction()

# libpathime_install_vendored_headers(<destination-subdir> <files>...)
#
# A vendored library's public headers, installed only under
# LIBPATHIME_INSTALL_VENDORED. The default install ships none: `<pathime/pathime.h>`
# is the whole interface, and a header for a library that is not on the link
# path would be an invitation to a link error.
function(libpathime_install_vendored_headers subdir)
  if(NOT LIBPATHIME_INSTALL_VENDORED)
    return()
  endif()
  install(FILES ${ARGN} DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/${subdir}")
endfunction()

# libpathime_install_package()
#
# The find_package() and pkg-config halves of the install. Called from the
# top-level CMakeLists after src/, because everything it exports has to exist.
function(libpathime_install_package)
  set(_cmakedir "${CMAKE_INSTALL_LIBDIR}/cmake/pathime")

  install(EXPORT ${LIBPATHIME_EXPORT_SET}
    FILE pathime-targets.cmake
    NAMESPACE libpathime::
    DESTINATION "${_cmakedir}")

  # The template is cmake/pathime-config.cmake.in — note its neighbour
  # cmake/pathime-config.h.in, which is a different thing with a similar name:
  # that one generates the public <pathime/config.h>.
  configure_package_config_file(
    "${PROJECT_SOURCE_DIR}/cmake/pathime-config.cmake.in"
    "${PROJECT_BINARY_DIR}/pathime-config.cmake"
    INSTALL_DESTINATION "${_cmakedir}"
    PATH_VARS LIBPATHIME_INSTALL_DATADIR)

  # SameMinorVersion, not SameMajorVersion, because the version is 0.x: before
  # 1.0 the minor number is where an incompatible change shows up, and the
  # looser rule would tell a consumer that 0.1 and 0.2 interchange.
  write_basic_package_version_file(
    "${PROJECT_BINARY_DIR}/pathime-config-version.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMinorVersion)

  install(FILES
    "${PROJECT_BINARY_DIR}/pathime-config.cmake"
    "${PROJECT_BINARY_DIR}/pathime-config-version.cmake"
    DESTINATION "${_cmakedir}")

  _libpathime_install_pkgconfig()
endfunction()

# The pkg-config file. Separate from the CMake package because it has one thing
# to work out that the export set derives for itself: the static link line.
function(_libpathime_install_pkgconfig)
  if(BUILD_SHARED_LIBS)
    # Everything the vendored libraries need is resolved through libpathime's
    # own DT_NEEDED and RPATH, so a consumer's link line is one -l.
    set(PATHIME_PC_CFLAGS "")
    set(PATHIME_PC_LIBS_PRIVATE "")
    set(PATHIME_PC_REQUIRES_PRIVATE "")
  else()
    set(PATHIME_PC_CFLAGS " -DPATHIME_STATIC")

    # The vendored archives, in the order they were registered, which each port
    # chooses to be dependents-before-dependencies — what a static link line
    # needs and what the CMake export set works out on its own.
    get_property(_vendored GLOBAL PROPERTY LIBPATHIME_VENDORED_TARGETS)
    set(_libs "-L\${libdir}/pathime")
    foreach(_tgt ${_vendored})
      get_target_property(_type ${_tgt} TYPE)
      if(_type STREQUAL "INTERFACE_LIBRARY")
        continue()   # carries no archive of its own
      endif()
      get_target_property(_name ${_tgt} OUTPUT_NAME)
      if(NOT _name)
        set(_name "${_tgt}")
      endif()
      list(APPEND _libs "-l${_name}")
    endforeach()
    if(NOT WIN32)
      list(APPEND _libs "-ldl")   # src/module_path.cc; see src/CMakeLists.txt
    endif()

    # libpathime is C++ behind a C header, so a C program linking the static
    # archive has to link the C++ runtime as well — the symbols are in the
    # archive, their implementation is not. The CMake package says the same
    # thing through IMPORTED_LINK_INTERFACE_LANGUAGES, which is why a consumer
    # there only has to enable_language(CXX); pkg-config has no equivalent and
    # wants the library named. Ask the toolchain rather than assuming libstdc++.
    foreach(_implicit ${CMAKE_CXX_IMPLICIT_LINK_LIBRARIES})
      if(_implicit MATCHES "(^|/)(lib)?(std)?c\\+\\+")
        list(APPEND _libs "-l${_implicit}")
      endif()
    endforeach()
    list(JOIN _libs " " PATHIME_PC_LIBS_PRIVATE)
    string(PREPEND PATHIME_PC_LIBS_PRIVATE " ")

    set(_requires "")
    if(LIBPATHIME_WITH_PYZY OR LIBPATHIME_WITH_TABLE)
      list(APPEND _requires sqlite3)
    endif()
    if(LIBPATHIME_WITH_PYZY)
      list(APPEND _requires glib-2.0)
      if(NOT WIN32 AND NOT APPLE)
        list(APPEND _requires uuid)
      endif()
    endif()
    list(JOIN _requires ", " PATHIME_PC_REQUIRES_PRIVATE)
    if(PATHIME_PC_REQUIRES_PRIVATE)
      string(PREPEND PATHIME_PC_REQUIRES_PRIVATE " ")
    endif()
  endif()

  configure_file(
    "${PROJECT_SOURCE_DIR}/cmake/pathime.pc.in"
    "${PROJECT_BINARY_DIR}/pathime.pc"
    @ONLY)
  install(FILES "${PROJECT_BINARY_DIR}/pathime.pc"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
endfunction()
