# The libpathime library target. docs/source-layout.md is the map: what each
# file is for and the conventions it follows.
#
# Core files sit directly in src/ — the public API entry points and the shared
# machinery are one layer, deliberately not split into api/ vs core/: the entry
# points are thin validators over the internal objects and would have nothing
# to themselves. The seam that matters is backend.h, the internal interface
# each adapter under engines/ implements.

add_library(pathime
  init.cc
  engine.cc
  context.cc
  composition.cc
  candidates.cc
  options.cc
  keys.cc
  module_path.cc
  paths.cc
  punctuation.cc
  utf8.cc
)
add_library(libpathime::pathime ALIAS pathime)

# The build-wide default is C++11 (pyzy's floor, set in LibpathimeOptions);
# our own code is C++17. Requested on this target only so it never leaks into
# the vendored trees.
target_compile_features(pathime PRIVATE cxx_std_17)

# Public include directories: source tree for <pathime/pathime.h>, binary tree
# for the generated <pathime/config.h>. Both are carried on the library target
# itself; there is no separate headers-only target to keep in step with it.
target_include_directories(pathime PUBLIC
  "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
  "$<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

# Internal headers are included relative to src/ from any depth, so an engine
# file writes #include "backend.h", not a ../../ chain.
target_include_directories(pathime PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

# PATHIME_BUILD flips PATHIME_API to its export form while the library itself
# compiles; this is the only place that defines it. A static build instead
# needs PATHIME_STATIC on both sides of the boundary, hence PUBLIC.
target_compile_definitions(pathime PRIVATE PATHIME_BUILD)
if(NOT BUILD_SHARED_LIBS)
  target_compile_definitions(pathime PUBLIC PATHIME_STATIC)
endif()

# module_path.cc asks the loader which file it is in, so that the resource
# directory can default to one beside it. That is dladdr() everywhere but
# Windows, which glibc kept in libdl until 2.34.
if(NOT WIN32)
  target_link_libraries(pathime PRIVATE ${CMAKE_DL_LIBS})
endif()

# --- Engine adapters: one directory per backend, gated on the same
# LIBPATHIME_WITH_* options that build the backends themselves. Core files
# compile regardless and branch on <pathime/config.h> where they must.
#
# Each block wires both the link libraries and the include path the adapter
# needs to reach that backend's *public* headers — never its internal ones,
# which is what separates these blocks from the equivalents under
# tests/<backend>/CMakeLists.txt.

if(LIBPATHIME_WITH_HANGUL)
  target_sources(pathime PRIVATE engines/hangul/hangul_backend.cc)
  target_link_libraries(pathime PRIVATE hangul)
  # libhangul's target publishes no interface include directory (its own
  # CMakeLists sets only a PRIVATE one), so consumers name it themselves.
  target_include_directories(pathime PRIVATE
    "${PROJECT_SOURCE_DIR}/engines/libhangul/hangul")
endif()

if(LIBPATHIME_WITH_ANTHY)
  target_sources(pathime PRIVATE
    engines/anthy/anthy_backend.cc
    engines/anthy/romaji.cc)
  target_link_libraries(pathime PRIVATE anthy-unicode anthydic-unicode)
  # <anthy/anthy.h> lives at the submodule root. Unlike the vendored test
  # suites the adapter needs nothing else: the public header is self-contained,
  # so the port's generated <config.h> stays out of our include path and none
  # of anthy's internal headers are reachable from here.
  target_include_directories(pathime PRIVATE "${ANTHY_TEST_SRC_DIR}")
endif()

if(LIBPATHIME_WITH_PYZY)
  target_sources(pathime PRIVATE
    engines/pyzy/pyzy_backend.cc
    engines/pyzy/observer.cc)
  target_link_libraries(pathime PRIVATE pyzy)

  # pyzy's src/String.h shadows the C library's <string.h> on a
  # case-insensitive filesystem, so pyzy's source directory must never appear
  # in an -I. The adapter needs only the public headers, so they are staged
  # into the layout `cmake --install` produces and included exactly as an
  # outside consumer would: <PyZy/InputContext.h>. The staging directory holds
  # nothing else, so there is nothing for a system header to collide with.
  #
  # This is the same trick tests/pyzy/CMakeLists.txt uses, minus the generated
  # forwarding headers — those exist there to reach pyzy's *private* headers,
  # which an adapter has no business touching.
  if(NOT PYZY_SRC_DIR)
    message(FATAL_ERROR
      "src: PYZY_SRC_DIR is not set. It is published by "
      "cmake/ports/pyzy/CMakeLists.txt, which must be processed first.")
  endif()
  # DataDir.h joins them: it is how the adapter tells pyzy where the database
  # is, and it is pyzy's only header that upstream does not also have.
  file(COPY
    "${PYZY_SRC_DIR}/Const.h"
    "${PYZY_SRC_DIR}/DataDir.h"
    "${PYZY_SRC_DIR}/InputContext.h"
    "${PYZY_SRC_DIR}/Variant.h"
    DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/include/PyZy")
  target_include_directories(pathime PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/include")
endif()

# The table-driven engine. Unlike the three above it wraps nothing — the
# sources here are the engine — so the block wires a library rather than a
# submodule, and the include path it needs is its own, already set above.
#
# The split inside engines/table/ is the one docs/source-layout.md records:
# table_backend.cc is the only file that includes backend.h, and the rest
# (source parsing, the database, ranking, variant classification) knows nothing
# about compositions or options. That is what lets tools/table-compile build
# the same files without any of the engine.
if(LIBPATHIME_WITH_TABLE)
  target_sources(pathime PRIVATE
    engines/table/table_backend.cc
    engines/table/table_db.cc
    engines/table/table_properties.cc
    engines/table/table_source.cc
    engines/table/ranking.cc
    engines/table/variants.cc)
  target_link_libraries(pathime PRIVATE SQLite::SQLite3)
endif()

# The ABI version. Pre-1.0 the SOVERSION tracks the minor — `libpathime.so.0.1`
# — because the minor is where an incompatible change lands before 1.0, which
# is exactly what cmake/LibpathimeInstall.cmake's package version file says by
# requiring a matching minor: the loader and the package file now make the
# same promise. The cost is that every 0.x minor bump forces consumers to
# relink even when the ABI happened to survive. At 1.0 this becomes the major
# alone (`libpathime.so.1`), and cross-version checking becomes worth
# automating. docs/design-history.md §15.
if(PROJECT_VERSION_MAJOR EQUAL 0)
  set(_pathime_soversion "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}")
else()
  set(_pathime_soversion "${PROJECT_VERSION_MAJOR}")
endif()
set_target_properties(pathime PROPERTIES
  VERSION "${PROJECT_VERSION}"
  SOVERSION "${_pathime_soversion}")

# The installed library, and the export set that cmake/LibpathimeInstall.cmake
# turns into pathime-targets.cmake. What a consumer's include path ends up being
# is the `$<INSTALL_INTERFACE:>` on the target above; there is no INCLUDES
# DESTINATION here, because it appends the same directory a second time.
#
# The private link libraries above reach the export set through
# libpathime_install_vendored(), which puts every vendored target in it. What
# the generated targets file then says about them differs by link mode and is
# right both times: archives on a static consumer's link line, and nothing but
# an RPATH hint for a shared one. cmake/LibpathimeInstall.cmake has the detail.
# The component split is what cpack packages select from
# (cmake/LibpathimePackage.cmake): `runtime` is what every artifact carries,
# `devel` is what only the library package adds. The namelink is devel — but
# note it is not only for the link editor: BUILD.md's install section names
# runtime loaders that probe `libpathime.so` by that exact spelling, which is
# why the library package's component list includes devel rather than trying
# to ship a runtime-only tree.
libpathime_set_install_rpath(pathime ${LIBPATHIME_VENDORED_SUBDIR})
install(TARGETS pathime
  EXPORT ${LIBPATHIME_EXPORT_SET}
  ${LIBPATHIME_RUNTIME_DEP_SET_ARGS}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT runtime
          NAMELINK_COMPONENT devel
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT devel)

# The table databases, compiled from engines/ibus-table-chinese by the tool in
# tools/. Declared here rather than there because an add_custom_command output
# must be consumed by a target in the same directory, and the target that
# consumes these is the staging one below. The tool itself is added after this
# directory; a generator expression and a target-name dependency both resolve
# at generate time, so the ordering does not matter.
libpathime_compile_tables()

# The read-only data the engines open, into the pathime-data/ beside the
# installed library that pathime_init_params_t::resource_dir finds by default.
libpathime_install_runtime_data()

# The same files in the build tree, so that the demo and the tests reach them
# exactly as a client does. Only needed when there is something here to run
# them: an embedder gets its copy from `cmake --install`.
if(LIBPATHIME_BUILD_TESTS OR LIBPATHIME_BUILD_DEMO)
  libpathime_stage_runtime_data()
endif()
