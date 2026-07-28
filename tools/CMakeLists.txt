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
endif()
