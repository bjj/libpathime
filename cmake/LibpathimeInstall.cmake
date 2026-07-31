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
#   <prefix>/lib/pathime-data/...                 (bin/ for a DLL or a static
#                                                  build; the rule is below)
#   <prefix>/lib/cmake/pathime/*.cmake
#   <prefix>/lib/pkgconfig/pathime.pc
#
# The vendored libraries keep their own names but live in a directory nothing
# else searches; libpathime reaches them through an RPATH of `$ORIGIN/pathime`
# (`@loader_path/pathime` on macOS). No vendored header is installed at all,
# because nothing outside this build compiles against one — BUILD.md,
# "Consuming the library", is the promise this keeps. Windows has no RPATH, so
# there the vendored DLLs sit beside `pathime.dll` in `bin/`: the same
# arrangement reached by the other platform's means, since the directory of the
# loading module is what both mechanisms name.
#
# What the private directory does not change is their SONAMEs: ours are still
# `libanthy-unicode.so.0` and the rest, so a process that has already loaded a
# distribution's copy of one of those names satisfies libpathime's `DT_NEEDED`
# with it — an RPATH is consulted only when nothing by that name is loaded yet.
# That is an exposure for an embedder who puts libpathime inside a larger process
# rather than running it as an engine of its own; an engine that owns its own
# process, which is what this library is for, has nothing else in the address
# space to have pulled the system copies in. Closing it would mean giving the
# vendored libraries names of our own, which the private directory deliberately
# does not do — the directory settles where they sit, not what they are called.
#
# They stay separate libraries rather than being absorbed into libpathime, and
# that is a licensing decision as much as a layout one. All three are LGPL-2.1
# (THIRD-PARTY.md): kept as separate replaceable files, an application shipping
# a shared libpathime satisfies the relinking clause by that fact alone. A
# static build ships them as separate archives in the same private directory and
# names them in the .pc's `Libs.private`, so they reach the embedder's own link
# line; the code then lands inside the embedder's binary and the obligation is
# theirs to read about. Either way this build never folds them into libpathime.
#
# LIBPATHIME_INSTALL_VENDORED=ON puts the libraries and their headers in the
# ordinary system places instead, for a packager who intends exactly the
# system-wide install this default avoids and has decided to deal with the
# consequences. It changes three things, and they are the three that name the
# private directory: where the libraries are installed, the RPATH that reaches
# them (there is none — the loader already searches where they are), and the .pc's
# -L. Nothing that gets compiled depends on which way it is set.

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# The private directory's name, in one place, because three rules have to mean
# the same directory by it: the install destination, the RPATH that reaches it,
# and the .pc's -L. Empty means there is no private directory — the vendored
# libraries go straight into the system library directory.
if(LIBPATHIME_INSTALL_VENDORED)
  set(LIBPATHIME_VENDORED_SUBDIR "")
  set(LIBPATHIME_VENDORED_LIBDIR "${CMAKE_INSTALL_LIBDIR}")
else()
  set(LIBPATHIME_VENDORED_SUBDIR "pathime")
  set(LIBPATHIME_VENDORED_LIBDIR "${CMAKE_INSTALL_LIBDIR}/${LIBPATHIME_VENDORED_SUBDIR}")
endif()

# The runtime directory is the same either way: a DLL is found beside the module
# that loads it, so a vendored DLL has nowhere else it could go.
set(LIBPATHIME_VENDORED_BINDIR "${CMAKE_INSTALL_BINDIR}")

# The external runtime-DLL closure, Windows-shared only. The build links DLLs
# it does not build — pyzy's glib and friends, from vcpkg — and vcpkg's
# applocal step stages them only beside the *build* tree's binaries, so an
# install that did not carry them would produce a pathime.dll that cannot
# load. Every installed runtime target joins this dependency set, and
# libpathime_install_package() installs the set's closure, computed at install
# time. Computed rather than hand-listed on purpose: a list of the current
# five names goes stale silently on a vcpkg baseline bump, where the
# dependency-set form encodes the rule and fails loudly.
# docs/ci-and-release-plan.md 4.3c has the reasoning, the alternatives
# considered, and the licence consequences of shipping the closure.
if(WIN32 AND BUILD_SHARED_LIBS)
  set(LIBPATHIME_RUNTIME_DEP_SET_ARGS RUNTIME_DEPENDENCY_SET pathime-runtime)
else()
  set(LIBPATHIME_RUNTIME_DEP_SET_ARGS "")
endif()

# Where pathime-data lands, and the single place that decides it: beside the
# module that resolves it, because that is what makes the default resource_dir
# correct for an installed tree. src/module_path.cc asks the loader which file a
# known address inside libpathime came from, so the answer follows from how
# libpathime was linked and not from the platform:
#
#   * a shared libpathime on ELF/Mach-O is its own module in CMAKE_INSTALL_LIBDIR
#     — the data goes beside it there;
#   * a DLL and a static libpathime both resolve to something in
#     CMAKE_INSTALL_BINDIR, the DLL because that is where a DLL has to live and a
#     static build because the address is inside the consumer's own executable.
#
# The static case is the one worth stating: the executable is the consumer's and
# this build never installs it, so `bin/` here is where the default *would* find
# the data, and a consumer whose program lands somewhere else copies the
# directory to sit beside it — PATHIME_DATA_DIR and the .pc's `datadir` are how
# they locate the copy. cmake/LibpathimeRuntimeData.cmake fills the directory and
# applies the same rule to the build tree; this is the arithmetic about where it
# is, kept here with the rest of the layout so that the CMake package and the .pc
# file can name it too.
#
# CMAKE_INSTALL_FULL_* rather than the prefix and the relative form glued
# together, because GNUInstallDirs allows an absolute CMAKE_INSTALL_LIBDIR and
# gluing then doubles the prefix.
if(BUILD_SHARED_LIBS AND NOT WIN32)
  set(LIBPATHIME_INSTALL_DATADIR "${CMAKE_INSTALL_LIBDIR}/${LIBPATHIME_RUNTIME_DATA_DIRNAME}")
  set(LIBPATHIME_INSTALL_FULL_DATADIR
      "${CMAKE_INSTALL_FULL_LIBDIR}/${LIBPATHIME_RUNTIME_DATA_DIRNAME}")
else()
  set(LIBPATHIME_INSTALL_DATADIR "${CMAKE_INSTALL_BINDIR}/${LIBPATHIME_RUNTIME_DATA_DIRNAME}")
  set(LIBPATHIME_INSTALL_FULL_DATADIR
      "${CMAKE_INSTALL_FULL_BINDIR}/${LIBPATHIME_RUNTIME_DATA_DIRNAME}")
endif()

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

# libpathime_set_install_rpath(<target> [<subdirectory>...])
#
# Point an installed binary at its siblings, and at each named subdirectory
# beside it. Set per target rather than through CMAKE_INSTALL_RPATH so that a
# tree which pulls libpathime in with add_subdirectory keeps its own RPATH
# policy for its own targets.
#
# Only libpathime passes a subdirectory: it is the one binary that reaches into
# the private vendored directory. The vendored libraries are already in it, and
# what they need from an RPATH is each other — anthy is three shared libraries
# over three static ones and resolves its own siblings.
function(libpathime_set_install_rpath tgt)
  if(WIN32)
    return()   # no RPATH; see the header comment
  endif()
  if(LIBPATHIME_INSTALL_VENDORED)
    # Nothing to reach. Everything is in the system library directory, which the
    # loader searches already, and an RPATH naming it is the one thing a
    # distribution's packaging checks reject outright (Debian's
    # binary-or-shlib-defines-rpath, Fedora's check-rpaths) — on the single
    # option that exists in order to produce a distribution package.
    return()
  endif()
  if(APPLE)
    set(_origin "@loader_path")
  else()
    set(_origin "$ORIGIN")
  endif()
  set(_rpath "${_origin}")
  foreach(_sub ${ARGN})
    list(APPEND _rpath "${_origin}/${_sub}")
  endforeach()

  # BUILD_RPATH as well as INSTALL_RPATH, and that is not belt-and-braces.
  # CMake rewrites a binary's RUNPATH in place at install time, so it pads the
  # build-tree string out to the length the install string will need — with ':'
  # characters, and an empty RUNPATH component is the current working directory
  # to glibc's loader. A vendored library that needs no build RPATH of its own
  # would otherwise ship a RUNPATH of nothing *but* separators, so a build-tree
  # binary run from a directory containing a file called libsqlite3.so.0 (or
  # libglib-2.0.so.0, or libuuid.so.1) would search cwd for every one of its
  # dependencies before the loader cache. Naming the same entries for the build
  # tree leaves nothing to pad, and they are right there too: everything this
  # build produces lands in one output directory, which is what `$ORIGIN` names.
  #
  # CMake still appends one trailing separator of its own — unconditionally, so
  # that the linker cannot share the RPATH's .dynstr entry with a symbol name —
  # and that last empty component cannot be removed while the RPATH is rewritten
  # at install time rather than relinked. Only BUILD_WITH_INSTALL_RPATH would,
  # by discarding the build tree's computed link paths, which is how a
  # build-tree pyzy finds a glib that is not in a default directory. One
  # last-searched cwd entry in a binary that is never installed is the better
  # end of that trade, and it is what every CMake target with an INSTALL_RPATH
  # carries.
  set_property(TARGET ${tgt} APPEND PROPERTY INSTALL_RPATH ${_rpath})
  set_property(TARGET ${tgt} APPEND PROPERTY BUILD_RPATH ${_rpath})
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
      ${LIBPATHIME_RUNTIME_DEP_SET_ARGS}
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

  # The Windows POSIX shim joins the export set, but only in a static build.
  # cmake/LibpathimeCompat.cmake links it PRIVATE into every vendored target,
  # and a static library records even a PRIVATE dependency in its
  # INTERFACE_LINK_LIBRARIES as `$<LINK_ONLY:...>` — so CMake requires it in the
  # set, and a consumer genuinely needs the archive on their link line. A shared
  # build absorbs it into the DLLs and needs nothing.
  #
  # Registered here rather than beside the target, because the vendored list is
  # ordered dependents-before-dependencies for the static link line and this is
  # the one thing all of them depend on: it has to come last.
  if(TARGET libpathime_win32compat AND NOT BUILD_SHARED_LIBS)
    libpathime_install_vendored(libpathime_win32compat)
  endif()

  # The closure of the runtime dependency set the install(TARGETS) rules
  # filled — LIBPATHIME_RUNTIME_DEP_SET_ARGS above is what it is for. The
  # filters are the policy: api-ms-/ext-ms- are the CRT's virtual DLL names,
  # satisfied by the loader rather than by a file; anything resolving to
  # system32 is the operating system's to provide; what remains resolves from
  # the vcpkg installed tree, named through DIRECTORIES so resolution does not
  # lean on the installing user's PATH. Lowercase regexes suffice — DLL names
  # are lowercased before the filters see them — and targets this build
  # produces never enter the closure, so the vendored DLLs cannot be
  # double-installed.
  if(LIBPATHIME_RUNTIME_DEP_SET_ARGS)
    set(_vcpkg_dirs "")
    if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
      set(_vcpkg_dirs DIRECTORIES
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}$<$<CONFIG:Debug>:/debug>/bin")
    endif()
    install(RUNTIME_DEPENDENCY_SET pathime-runtime
      PRE_EXCLUDE_REGEXES "^api-ms-" "^ext-ms-"
      POST_EXCLUDE_REGEXES "[/\\\\][Ss]ystem32[/\\\\]"
      ${_vcpkg_dirs}
      RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
  endif()

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
  # The paths, relocatable when the layout allows it. A release archive is
  # extracted at whatever prefix the downloader chooses, so a .pc that recorded
  # the build machine's prefix would hand every consumer paths onto a machine
  # they do not have; deriving prefix from ${pcfiledir} keeps the file true
  # wherever the tree lands, and matches how the CMake package config and the
  # library's own data lookup already locate themselves. The test is the three
  # directories the file's variables are built from: GNUInstallDirs allows any
  # of them to be absolute, and one absolute directory makes the whole file
  # configure-time-pinned — pinned wholesale rather than mixed, because a file
  # half anchored to ${pcfiledir} would come apart the first time the tree
  # moved. BINDIR is in the test only because a static or Windows layout puts
  # pathime-data under it.
  if(NOT IS_ABSOLUTE "${CMAKE_INSTALL_LIBDIR}"
     AND NOT IS_ABSOLUTE "${CMAKE_INSTALL_INCLUDEDIR}"
     AND NOT IS_ABSOLUTE "${CMAKE_INSTALL_BINDIR}")
    file(RELATIVE_PATH _pc_to_prefix
         "${CMAKE_INSTALL_FULL_LIBDIR}/pkgconfig" "${CMAKE_INSTALL_PREFIX}")
    string(REGEX REPLACE "/+$" "" _pc_to_prefix "${_pc_to_prefix}")
    set(PATHIME_PC_PREFIX "\${pcfiledir}/${_pc_to_prefix}")
    set(PATHIME_PC_LIBDIR "\${prefix}/${CMAKE_INSTALL_LIBDIR}")
    set(PATHIME_PC_INCLUDEDIR "\${prefix}/${CMAKE_INSTALL_INCLUDEDIR}")
    set(PATHIME_PC_DATADIR "\${prefix}/${LIBPATHIME_INSTALL_DATADIR}")
  else()
    set(PATHIME_PC_PREFIX "${CMAKE_INSTALL_PREFIX}")
    set(PATHIME_PC_LIBDIR "${CMAKE_INSTALL_FULL_LIBDIR}")
    set(PATHIME_PC_INCLUDEDIR "${CMAKE_INSTALL_FULL_INCLUDEDIR}")
    set(PATHIME_PC_DATADIR "${LIBPATHIME_INSTALL_FULL_DATADIR}")
  endif()

  # Every library is named through $<TARGET_LINKER_FILE_BASE_NAME> rather than by
  # its target or OUTPUT_NAME, because CMAKE_DEBUG_POSTFIX and a per-config
  # OUTPUT_NAME both rename the file a consumer has to link and only the
  # generator knows the answer — `-lpathime` in a `-DCMAKE_DEBUG_POSTFIX=d`
  # install names an archive that is not there. That is also why the file is
  # finished by file(GENERATE) below instead of configure_file alone.
  set(PATHIME_PC_LIB "-l$<TARGET_LINKER_FILE_BASE_NAME:pathime>")

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
    if(LIBPATHIME_VENDORED_SUBDIR)
      set(_libs "-L\${libdir}/${LIBPATHIME_VENDORED_SUBDIR}")
    else()
      set(_libs "")   # already covered by the -L${libdir} in Libs:
    endif()
    foreach(_tgt ${_vendored})
      get_target_property(_type ${_tgt} TYPE)
      if(_type STREQUAL "INTERFACE_LIBRARY")
        continue()   # carries no archive of its own
      endif()
      list(APPEND _libs "-l$<TARGET_LINKER_FILE_BASE_NAME:${_tgt}>")
    endforeach()

    # The system libraries this build links, taken from the variables the build
    # itself uses so that the two cannot drift:
    #
    #  * CMAKE_DL_LIBS for src/module_path.cc's dladdr — `dl` on glibc, and
    #    *empty* on Darwin and the BSDs, where naming -ldl outright is a link
    #    error for a library the build never linked;
    #  * libm for anthy's splitter, which calls pow/exp (lattice.c) and sqrt
    #    (metaword.c). Easy to miss because sqlite3.pc drags -lm in for any build
    #    with a SQLite backend, and because glibc 2.34 merged libm into libc —
    #    so leaving it out only breaks the older distributions, musl, and cross
    #    toolchains;
    #  * Rpcrt4 for the UuidCreate in cmake/compat/win32/uuid_win.c.
    foreach(_syslib ${CMAKE_DL_LIBS})
      list(APPEND _libs "-l${_syslib}")
    endforeach()
    if(LIBPATHIME_WITH_ANTHY AND ANTHY_LIBM)
      list(APPEND _libs "-lm")
    endif()
    if(TARGET libpathime_win32compat)
      list(APPEND _libs "-lRpcrt4")
    endif()

    # libpathime is C++ behind a C header, so a C program linking the static
    # archive has to link the C++ runtime as well — the symbols are in the
    # archive, their implementation is not. The CMake package says the same
    # thing through IMPORTED_LINK_INTERFACE_LANGUAGES, which is why a consumer
    # there only has to enable_language(CXX); pkg-config has no equivalent and
    # wants the library named. Ask the toolchain rather than assuming libstdc++.
    #
    # An entry may be an absolute path rather than a name — that is what
    # -static-libstdc++ reports, and NDK, Apple SDK and the vendor compilers
    # list paths as a matter of course — and a path links as itself. Every match
    # is kept rather than the first, because a toolchain that splits its runtime
    # across libc++ and libc++abi lists both and needs both.
    foreach(_implicit ${CMAKE_CXX_IMPLICIT_LINK_LIBRARIES})
      if(_implicit MATCHES "(^|/)(lib)?(std)?c\\+\\+")
        if(IS_ABSOLUTE "${_implicit}")
          list(APPEND _libs "${_implicit}")
        else()
          list(APPEND _libs "-l${_implicit}")
        endif()
      endif()
    endforeach()
    list(JOIN _libs " " PATHIME_PC_LIBS_PRIVATE)
    string(PREPEND PATHIME_PC_LIBS_PRIVATE " ")

    # The same modules, and the same version floors, that
    # cmake/LibpathimeDependencies.cmake probes and the ports link. A floor
    # dropped here would accept a consumer the CMake half rejects.
    set(_requires "")
    if(LIBPATHIME_WITH_PYZY OR LIBPATHIME_WITH_TABLE)
      list(APPEND _requires sqlite3)
    endif()
    if(LIBPATHIME_WITH_PYZY)
      list(APPEND _requires "glib-2.0 >= 2.24.0")
      if(NOT WIN32)
        list(APPEND _requires uuid)   # cmake/ports/pyzy links it everywhere but Windows
      endif()
    endif()
    list(JOIN _requires ", " PATHIME_PC_REQUIRES_PRIVATE)
    if(PATHIME_PC_REQUIRES_PRIVATE)
      string(PREPEND PATHIME_PC_REQUIRES_PRIVATE " ")
    endif()
  endif()

  # Two stages: configure_file for the @VARIABLES@, then file(GENERATE) for the
  # generator expressions above. The output is per-configuration because the
  # library names are — a multi-config generator installs the one matching the
  # configuration being installed, and a single-config generator has only the
  # one directory.
  configure_file(
    "${PROJECT_SOURCE_DIR}/cmake/pathime.pc.in"
    "${PROJECT_BINARY_DIR}/pathime.pc.configured"
    @ONLY)
  file(GENERATE
    OUTPUT "${PROJECT_BINARY_DIR}/pkgconfig/$<CONFIG>/pathime.pc"
    INPUT "${PROJECT_BINARY_DIR}/pathime.pc.configured")
  install(FILES "${PROJECT_BINARY_DIR}/pkgconfig/$<CONFIG>/pathime.pc"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
endfunction()
