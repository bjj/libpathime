# Build-time tools. Not installed and not part of the library's contract: they
# exist to turn data this repository ships into the form the library reads.
#
# pathime-table-compile compiles the source tables of engines/ibus-table-chinese
# into the SQLite databases the table engine opens. It compiles the engine's
# data-layer sources directly rather than linking libpathime, exactly as
# tests/core/ does and for the same reason — those helpers carry no PATHIME_API
# and a shared build does not export them, and decorating them to make a build
# tool work would widen the ABI for no client's benefit.

if(LIBPATHIME_WITH_TABLE)
  add_executable(pathime-table-compile
    table-compile/main.cc
    "${PROJECT_SOURCE_DIR}/src/engines/table/table_source.cc"
    "${PROJECT_SOURCE_DIR}/src/engines/table/table_properties.cc"
    "${PROJECT_SOURCE_DIR}/src/engines/table/table_db.cc"
    "${PROJECT_SOURCE_DIR}/src/engines/table/coverage.cc"
    "${PROJECT_SOURCE_DIR}/src/paths.cc"
    "${PROJECT_SOURCE_DIR}/src/utf8.cc")

  target_compile_features(pathime-table-compile PRIVATE cxx_std_17)

  # Rooted at src/ so the shared sources use the same include spellings here as
  # they do in the library, and at include/ for <pathime/pathime.h>, which
  # table_properties.h needs for the option enum.
  target_include_directories(pathime-table-compile PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_BINARY_DIR}/include")

  # PATHIME_STATIC because this program is not linking the shared library: the
  # public header's PATHIME_API must expand to nothing rather than to an import
  # declaration for a DLL that is not involved.
  target_compile_definitions(pathime-table-compile PRIVATE PATHIME_STATIC)

  target_link_libraries(pathime-table-compile PRIVATE SQLite::SQLite3)

  # The map the tool trims against. libpathime_table_coverage_definitions() is in
  # cmake/LibpathimeOptions.cmake beside the option itself; the test that
  # compiles coverage.cc uses the same call, so the tool and the test can never
  # disagree about which map they are talking about.
  libpathime_table_coverage_definitions(_coverage_defines)
  target_compile_definitions(pathime-table-compile PRIVATE ${_coverage_defines})

  # ---------------------------------------------------------------------------
  # Regenerating a glyph-coverage map
  # ---------------------------------------------------------------------------
  #
  # src/engines/table/coverage_data_*.h are checked in, and the ordinary build
  # never regenerates them. That is the whole point: reading a font at build time
  # would make a compiled table a function of which fonts the build machine
  # happens to have, so two builds of the same commit would ship different data.
  # The same bargain src/engines/table/variants_data.h makes with Unicode data.
  #
  # This target exists for the occasional deliberate refresh — a newer Noto, a
  # newer Windows font set, or a different coverage policy. OFF by default
  # because regenerating is a decision rather than a build step.
  #
  #   cmake -DLIBPATHIME_TABLE_REGENERATE_COVERAGE=ON \
  #         -DLIBPATHIME_TABLE_COVERAGE=noto \
  #         -DLIBPATHIME_TABLE_COVERAGE_FONT=/path/to/font.ttc ...
  #   cmake --build . --target pathime-table-coverage
  #
  # It rewrites the header for whichever map LIBPATHIME_TABLE_COVERAGE names, in
  # the *source* tree, because the output is a checked-in file that wants
  # reviewing in a diff rather than a build artifact. Regenerating under
  # LIBPATHIME_TABLE_COVERAGE=none is refused: there is no header to write.
  option(LIBPATHIME_TABLE_REGENERATE_COVERAGE
    "Offer a target that regenerates the table glyph-coverage map" OFF)

  # A list, not a single path: the Windows map is the union of several in-box
  # faces, because no one of them covers what the platform can draw. An entry may
  # be PATH or PATH#FACE to pick out of a TrueType collection.
  if(WIN32)
    set(_coverage_font_default
      "$ENV{SystemRoot}/Fonts/simsun.ttc"
      "$ENV{SystemRoot}/Fonts/msyh.ttc"
      "$ENV{SystemRoot}/Fonts/msjh.ttc"
      "$ENV{SystemRoot}/Fonts/YuGothR.ttc"
      "$ENV{SystemRoot}/Fonts/malgun.ttf")
  else()
    set(_coverage_font_default
      "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc")
  endif()

  set(LIBPATHIME_TABLE_COVERAGE_FONT "${_coverage_font_default}"
    CACHE STRING "Fonts whose combined coverage bounds the compiled tables")

  if(LIBPATHIME_TABLE_REGENERATE_COVERAGE)
    if(LIBPATHIME_TABLE_COVERAGE STREQUAL "none")
      message(FATAL_ERROR
        "libpathime: LIBPATHIME_TABLE_REGENERATE_COVERAGE needs "
        "LIBPATHIME_TABLE_COVERAGE to name a map (noto or windows), not none.")
    endif()

    libpathime_find_python3("libpathime: coverage")
    if(NOT Python3_Interpreter_FOUND)
      message(FATAL_ERROR
        "libpathime: LIBPATHIME_TABLE_REGENERATE_COVERAGE needs Python 3, and "
        "neither FindPython3 nor the py launcher found one.")
    endif()

    if(LIBPATHIME_TABLE_COVERAGE STREQUAL "windows")
      set(_coverage_map_name "Windows in-box CJK")
    else()
      set(_coverage_map_name "Noto Sans CJK")
    endif()

    # --output rather than shell redirection: `>` inside a custom command is the
    # shell's, and VERBATIM quoting plus the generator differences between Ninja
    # and Visual Studio make it something better not relied on.
    add_custom_target(pathime-table-coverage
      COMMAND "${Python3_EXECUTABLE}"
              "${PROJECT_SOURCE_DIR}/tools/generate-coverage.py"
              --map "${_coverage_map_name}"
              --output "${PROJECT_SOURCE_DIR}/src/engines/table/coverage_data_${LIBPATHIME_TABLE_COVERAGE}.h"
              ${LIBPATHIME_TABLE_COVERAGE_FONT}
      VERBATIM
      COMMENT "libpathime: regenerating the ${_coverage_map_name} coverage map")
  endif()
endif()
