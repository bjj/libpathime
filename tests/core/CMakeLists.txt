# Internal-core tests, ctest names `core.<name>`.
#
# The peer of tests/api, and the distinction between them is what each links.
#
#   tests/api  links the built library and touches only exported symbols. It is
#              C11 on purpose: those tests double as proof that
#              <pathime/pathime.h> is consumable from strict C and that every
#              symbol a client needs is actually exported.
#   tests/core compiles the internal sources under test directly into the test
#              executable. Internal helpers carry no PATHIME_API, so a shared
#              build does not export them and there is nothing for a test to
#              link against — and giving them export decorations purely to make
#              them testable would widen the ABI for no client's benefit.
#
# Compiling the sources in twice is the deliberate trade. It costs a little
# build time and buys tests that can reach the seams where the encoding and
# option rules actually live, without any of it becoming public surface.
#
# Not gated on any LIBPATHIME_WITH_* option: everything here is core code that
# compiles identically whatever backends the build contains.

# One test executable per internal subject. `sources` lists the src/ files the
# test compiles in — name only what the subject needs, so a broken dependency
# shows up as a link error here rather than as a silently widening test binary.
function(libpathime_add_core_test name)
  cmake_parse_arguments(ARG "" "" "SOURCES" ${ARGN})
  add_executable(core_${name}_test "${name}_test.cc")
  target_sources(core_${name}_test PRIVATE ${ARG_SOURCES})
  target_include_directories(core_${name}_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_BINARY_DIR}/include")
  target_compile_features(core_${name}_test PRIVATE cxx_std_17)
  # The internal sources expect the same export decoration they are built with
  # in the library, so that a shared build's PATHIME_API does not flip to its
  # import form here.
  target_compile_definitions(core_${name}_test PRIVATE PATHIME_BUILD)
  add_test(NAME core.${name} COMMAND core_${name}_test)
endfunction()

libpathime_add_core_test(utf8 SOURCES "${PROJECT_SOURCE_DIR}/src/utf8.cc")

# core.options builds pathime_engine and pathime_context handles by hand — the
# only way to reach the two-level option store, the per-engine descriptors and
# the resolution rules without going through pathime_engine_create(), which can
# only ever hand back the engines this build's adapters happen to supply. What
# it then calls against those handles is the ordinary public API.
#
# Its source list is the whole library rather than a subset, and that is not
# laziness: options.cc reaches context.cc (refresh_composition, the reset that
# a resets_composition option performs) and init.cc (the initialization gate);
# context.cc reaches candidates.cc, composition.cc, keys.cc and engine.cc; and
# init.cc and engine.cc both reach the adapters through backend.h, so the
# per-backend arms come too, gated exactly as src/CMakeLists.txt gates them.
#
# The vendor include and link wiring is inherited from the pathime target
# rather than restated. Copying it would mean duplicating the pyzy header
# staging and the anthy include dir, and the copy would drift the first time
# either changes.
set(_lpi_options_sources
  "${PROJECT_SOURCE_DIR}/src/init.cc"
  "${PROJECT_SOURCE_DIR}/src/engine.cc"
  "${PROJECT_SOURCE_DIR}/src/context.cc"
  "${PROJECT_SOURCE_DIR}/src/composition.cc"
  "${PROJECT_SOURCE_DIR}/src/candidates.cc"
  "${PROJECT_SOURCE_DIR}/src/options.cc"
  "${PROJECT_SOURCE_DIR}/src/keys.cc"
  "${PROJECT_SOURCE_DIR}/src/utf8.cc")
if(LIBPATHIME_WITH_HANGUL)
  list(APPEND _lpi_options_sources
    "${PROJECT_SOURCE_DIR}/src/engines/hangul/hangul_backend.cc")
endif()
if(LIBPATHIME_WITH_ANTHY)
  list(APPEND _lpi_options_sources
    "${PROJECT_SOURCE_DIR}/src/engines/anthy/anthy_backend.cc"
    "${PROJECT_SOURCE_DIR}/src/engines/anthy/romaji.cc")
endif()
if(LIBPATHIME_WITH_PYZY)
  list(APPEND _lpi_options_sources
    "${PROJECT_SOURCE_DIR}/src/engines/pyzy/pyzy_backend.cc"
    "${PROJECT_SOURCE_DIR}/src/engines/pyzy/observer.cc")
endif()
libpathime_add_core_test(options SOURCES ${_lpi_options_sources})
target_include_directories(core_options_test PRIVATE
  $<TARGET_PROPERTY:pathime,INCLUDE_DIRECTORIES>)
target_link_libraries(core_options_test PRIVATE
  $<TARGET_PROPERTY:pathime,LINK_LIBRARIES>)

libpathime_add_core_test(composition SOURCES
  "${PROJECT_SOURCE_DIR}/src/composition.cc"
  "${PROJECT_SOURCE_DIR}/src/utf8.cc")
