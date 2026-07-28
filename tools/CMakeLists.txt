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

  # ---------------------------------------------------------------------------
  # Regenerating the glyph-coverage map
  # ---------------------------------------------------------------------------
  #
  # src/engines/table/coverage_data.h is checked in, and the ordinary build never
  # regenerates it. That is the whole point: reading a font at build time would
  # make a compiled table a function of which fonts the build machine happens to
  # have, so two builds of the same commit would ship different data. The same
  # bargain src/engines/table/variants_data.h makes with Unicode data.
  #
  # This target exists for the occasional deliberate refresh — a newer Noto, or
  # a different coverage policy. OFF by default because it needs Python 3 and
  # fontconfig, and because regenerating is a decision rather than a build step.
  #
  #   cmake -DLIBPATHIME_TABLE_REGENERATE_COVERAGE=ON \
  #         -DLIBPATHIME_TABLE_COVERAGE_FONT=/path/to/font.ttc ...
  #   cmake --build . --target pathime-table-coverage
  #
  # It writes into the *source* tree, because the output is a checked-in file
  # that wants reviewing in a diff, not a build artifact.
  option(LIBPATHIME_TABLE_REGENERATE_COVERAGE
    "Offer a target that regenerates the table glyph-coverage map" OFF)

  set(LIBPATHIME_TABLE_COVERAGE_FONT
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"
    CACHE FILEPATH "Font whose coverage bounds the compiled tables")

  if(LIBPATHIME_TABLE_REGENERATE_COVERAGE)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_custom_target(pathime-table-coverage
      COMMAND "${Python3_EXECUTABLE}"
              "${PROJECT_SOURCE_DIR}/tools/generate-coverage.py"
              "${LIBPATHIME_TABLE_COVERAGE_FONT}"
              > "${PROJECT_SOURCE_DIR}/src/engines/table/coverage_data.h"
      VERBATIM
      COMMENT "libpathime: regenerating table coverage map from ${LIBPATHIME_TABLE_COVERAGE_FONT}")
  endif()
endif()
