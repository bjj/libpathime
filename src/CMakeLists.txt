# The libpathime library target. docs/source-layout.md is the map: what each
# file is for, the conventions, and which choices are still open. docs/adapter-findings.md
# holds the adapter-layer findings the layout answers to.
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
  utf8.cc
)
add_library(libpathime::pathime ALIAS pathime)

# The build-wide default is C++11 (pyzy's floor, set in LibpathimeOptions);
# our own code is C++17. Requested on this target only so it never leaks into
# the vendored trees.
target_compile_features(pathime PRIVATE cxx_std_17)

# Public include directories: source tree for <pathime/pathime.h>, binary tree
# for the generated <pathime/config.h>. Folded here from the retired
# libpathime::headers interface target (docs/design-history.md §5).
target_include_directories(pathime PUBLIC
  "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
  "$<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

# Internal headers are included relative to src/ from any depth, so an engine
# file writes #include "backend.h", not a ../../ chain.
target_include_directories(pathime PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

# PATHIME_BUILD flips PATHIME_API to its export form while the library itself
# compiles (closes the "no one to define it" loose end from docs/design-history.md §5). A
# static build instead needs PATHIME_STATIC on both sides of the boundary,
# hence PUBLIC.
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
    engines/pyzy/observer.cc
    engines/pyzy/punctuation.cc)
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
  # which an adapter has no business touching. Copying from
  # PYZY_EFFECTIVE_SRC_DIR rather than engines/pyzy/src matters on Windows, where it is
  # the rewritten mirror the DLL was actually built from.
  if(NOT PYZY_EFFECTIVE_SRC_DIR)
    message(FATAL_ERROR
      "src: PYZY_EFFECTIVE_SRC_DIR is not set. It is published by "
      "cmake/ports/pyzy/CMakeLists.txt, which must be processed first.")
  endif()
  # DataDir.h joins them: it is the port's, not upstream's, and it is how the
  # adapter tells pyzy where the database is.
  file(COPY
    "${PYZY_EFFECTIVE_SRC_DIR}/Const.h"
    "${PYZY_EFFECTIVE_SRC_DIR}/DataDir.h"
    "${PYZY_EFFECTIVE_SRC_DIR}/InputContext.h"
    "${PYZY_EFFECTIVE_SRC_DIR}/Variant.h"
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

install(TARGETS pathime
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

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
