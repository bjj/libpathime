# The libpathime library target. docs/source-layout.md is the map: what each
# file is for, the conventions, and which choices are still open. TODO.md §2
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
  utf8.cc
)
add_library(libpathime::pathime ALIAS pathime)

# The build-wide default is C++11 (pyzy's floor, set in LibpathimeOptions);
# our own code is C++17. Requested on this target only so it never leaks into
# the vendored trees.
target_compile_features(pathime PRIVATE cxx_std_17)

# Public include directories: source tree for <pathime/pathime.h>, binary tree
# for the generated <pathime/config.h>. Folded here from the retired
# libpathime::headers interface target (TODO.md §5).
target_include_directories(pathime PUBLIC
  "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
  "$<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

# Internal headers are included relative to src/ from any depth, so an engine
# file writes #include "backend.h", not a ../../ chain.
target_include_directories(pathime PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

# PATHIME_BUILD flips PATHIME_API to its export form while the library itself
# compiles (closes the "no one to define it" loose end from TODO.md §5). A
# static build instead needs PATHIME_STATIC on both sides of the boundary,
# hence PUBLIC.
target_compile_definitions(pathime PRIVATE PATHIME_BUILD)
if(NOT BUILD_SHARED_LIBS)
  target_compile_definitions(pathime PUBLIC PATHIME_STATIC)
endif()

# --- Engine adapters: one directory per backend, gated on the same
# LIBPATHIME_WITH_* options that build the backends themselves. Core files
# compile regardless and branch on <pathime/config.h> where they must.
#
# Only the link libraries are wired here. The vendor *include* wiring for
# anthy and pyzy (port config dirs, generated forwarding headers) is not
# replicated yet — when an adapter first includes a vendor header, lift the
# already-solved pattern from tests/<backend>/CMakeLists.txt.

if(LIBPATHIME_WITH_HANGUL)
  target_sources(pathime PRIVATE engines/hangul/hangul_backend.cc)
  target_link_libraries(pathime PRIVATE hangul)
  # libhangul's target publishes no interface include directory (its own
  # CMakeLists sets only a PRIVATE one), so consumers name it themselves.
  target_include_directories(pathime PRIVATE
    "${PROJECT_SOURCE_DIR}/libhangul/hangul")
endif()

if(LIBPATHIME_WITH_ANTHY)
  target_sources(pathime PRIVATE
    engines/anthy/anthy_backend.cc
    engines/anthy/romaji.cc)
  target_link_libraries(pathime PRIVATE anthy-unicode anthydic-unicode)
endif()

if(LIBPATHIME_WITH_PYZY)
  target_sources(pathime PRIVATE
    engines/pyzy/pyzy_backend.cc
    engines/pyzy/observer.cc)
  target_link_libraries(pathime PRIVATE pyzy)
endif()

# engines/table/ holds only a README until the engine described by
# docs/ibus-table-spec.md is written; LIBPATHIME_WITH_TABLE is still forced
# off in LibpathimeDependencies.

install(TARGETS pathime
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
