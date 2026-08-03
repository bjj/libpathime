# Per-backend dependency discovery and gating.
#
# Philosophy: a fresh checkout should always *configure*. If an enabled backend
# is missing a dependency we warn and skip it, unless LIBPATHIME_REQUIRE_BACKENDS
# is set (then it is a hard error). A summary is printed at the end.

find_package(PkgConfig QUIET)

# libpathime_find_python3(<label>)
#
# find_package(Python3 COMPONENTS Interpreter) with the Windows fallback, setting
# Python3_EXECUTABLE and Python3_Interpreter_FOUND in the caller's scope.
#
# FindPython3 misses plenty of real Windows installs: the PATH entry is often the
# Store's stub launcher, and per-user registry entries written by conda & co.
# lack the values FindPython3 validates against. py.exe is the platform's own
# answer to "where is Python", so ask it before giving up.
#
# Shared because two things need it and neither is a hard dependency — pyzy's
# optional android.db, and the coverage-map regeneration target. Nothing here is
# needed by an ordinary build on any platform.
function(libpathime_find_python3 label)
  find_package(Python3 COMPONENTS Interpreter QUIET)

  if(NOT Python3_Interpreter_FOUND AND WIN32)
    find_program(LIBPATHIME_PY_LAUNCHER py)
    if(LIBPATHIME_PY_LAUNCHER)
      execute_process(
        COMMAND "${LIBPATHIME_PY_LAUNCHER}" -3 -c "import sys; sys.stdout.write(sys.executable)"
        OUTPUT_VARIABLE _py_exe RESULT_VARIABLE _py_rc ERROR_QUIET)
      if(_py_rc EQUAL 0 AND EXISTS "${_py_exe}")
        message(STATUS "${label}: using Python 3 via the py launcher: ${_py_exe}")
        set(Python3_EXECUTABLE "${_py_exe}")
        set(Python3_Interpreter_FOUND TRUE)
      endif()
    endif()
  endif()

  set(Python3_EXECUTABLE "${Python3_EXECUTABLE}" PARENT_SCOPE)
  set(Python3_Interpreter_FOUND "${Python3_Interpreter_FOUND}" PARENT_SCOPE)
endfunction()

# _lpi_gate(<KEY> <label> <missing-list> <hint>)
# Disables LIBPATHIME_WITH_<KEY> (or errors, per LIBPATHIME_REQUIRE_BACKENDS).
function(_lpi_gate key label missing hint)
  string(REPLACE ";" ", " _m "${missing}")
  if(LIBPATHIME_REQUIRE_BACKENDS)
    message(FATAL_ERROR
      "libpathime: ${label} backend is enabled but is missing: ${_m}\n  ${hint}")
  else()
    message(WARNING
      "libpathime: disabling ${label} backend — missing: ${_m}\n  ${hint}")
    set(LIBPATHIME_WITH_${key} OFF PARENT_SCOPE)
  endif()
endfunction()

# --- Japanese: anthy-unicode. No external libraries, but its dictionary is
#     produced by host tools compiled during the build, so cross-compilation is
#     not supported yet. ---
if(LIBPATHIME_WITH_ANTHY AND CMAKE_CROSSCOMPILING)
  _lpi_gate(ANTHY "Japanese (anthy-unicode)" "native toolchain"
    "anthy builds its dictionary with host-run codegen tools; cross-compiling needs a separate native tool build (not implemented yet).")
endif()

# --- Chinese: pyzy. Needs sqlite3 and a UUID source. (Its generated tables
#     are committed in-tree, so Python 3 is only needed for the optional
#     runtime database — probed inside the port, not required here.) ---
if(LIBPATHIME_WITH_PYZY)
  set(_pyzy_missing "")

  find_package(SQLite3 QUIET)
  if(NOT SQLite3_FOUND)
    list(APPEND _pyzy_missing "SQLite3")
  endif()

  # UUID: libuuid on Linux and the BSDs; on Windows the compat shim maps to
  # Rpcrt4 (always present); on macOS uuid_generate lives in libSystem and
  # <uuid/uuid.h> in the SDK — there is no uuid.pc and nothing to link, so the
  # only thing to verify is the header.
  if(APPLE)
    include(CheckIncludeFile)
    check_include_file(uuid/uuid.h LIBPATHIME_UUID_HEADER)
    if(NOT LIBPATHIME_UUID_HEADER)
      list(APPEND _pyzy_missing "uuid/uuid.h (macOS SDK)")
    endif()
  elseif(NOT WIN32)
    if(PkgConfig_FOUND)
      pkg_check_modules(LIBPATHIME_UUID QUIET uuid)
    endif()
    if(NOT LIBPATHIME_UUID_FOUND)
      list(APPEND _pyzy_missing "uuid (libuuid)")
    endif()
  endif()

  if(_pyzy_missing)
    _lpi_gate(PYZY "Chinese (pyzy)" "${_pyzy_missing}"
      "Debian/Ubuntu: sudo apt-get install libsqlite3-dev uuid-dev -- Windows: vcpkg install --triplet x64-windows-static-md sqlite3 (uuid via the bundled Rpcrt4 shim).")
  endif()
endif()

# --- libhangul: no external dependencies once external keyboards are disabled. ---

# --- Table-driven: our own engine, not a submodule, so the only thing to find
#     is SQLite. That is a genuine dependency rather than an incidental one:
#     the compiled table format of docs/ibus-table-mapping.md §4 *is* a SQLite
#     database, and reading one ibus-table produced is the whole point of
#     sharing the format. Probed independently of pyzy, which also uses SQLite
#     — either backend may be built without the other. ---
if(LIBPATHIME_WITH_TABLE)
  set(_table_missing "")
  find_package(SQLite3 QUIET)
  if(NOT SQLite3_FOUND)
    list(APPEND _table_missing "SQLite3")
  endif()

  # The tables themselves. Their source .txt lives in a submodule, and without
  # it the engine still builds and still opens a table a client names by path —
  # so this gates the shipped data, not the backend.
  if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/../engines/ibus-table-chinese/tables")
    message(STATUS
      "libpathime: engines/ibus-table-chinese is not checked out — the table "
      "engine will build but ship no tables. Run: git submodule update --init")
    set(LIBPATHIME_TABLE_DATA OFF CACHE INTERNAL "")
  else()
    set(LIBPATHIME_TABLE_DATA ON CACHE INTERNAL "")
  endif()

  if(_table_missing)
    _lpi_gate(TABLE "table-driven" "${_table_missing}"
      "Debian/Ubuntu: sudo apt-get install libsqlite3-dev -- Windows: vcpkg install --triplet x64-windows-static-md sqlite3.")
  endif()
endif()

message(STATUS "")
message(STATUS "libpathime backends:")
message(STATUS "  Korean   (libhangul)      : ${LIBPATHIME_WITH_HANGUL}")
message(STATUS "  Japanese (anthy-unicode)  : ${LIBPATHIME_WITH_ANTHY}")
message(STATUS "  Chinese  (pyzy)           : ${LIBPATHIME_WITH_PYZY}")
message(STATUS "  Table    (libpathime)     : ${LIBPATHIME_WITH_TABLE}")
# The coverage map decides which rows the shipped tables carry, and it is the one
# setting here whose default differs by platform. Printed so that a difference
# between two machines' tables is visible in the configure output rather than
# discovered later in a diff of the compiled data.
if(LIBPATHIME_WITH_TABLE)
  message(STATUS "    glyph coverage map      : ${LIBPATHIME_TABLE_COVERAGE}")
endif()
if(CMAKE_HOST_WIN32 AND (LIBPATHIME_WITH_PYZY OR LIBPATHIME_WITH_TABLE))
  if(LIBPATHIME_STATIC_SQLITE)
    message(STATUS "  SQLite                    : static")
  else()
    message(STATUS "  SQLite                    : shared (ships sqlite3.dll)")
  endif()
endif()
message(STATUS "  Shared libraries          : ${BUILD_SHARED_LIBS}")
message(STATUS "  Interactive demo          : ${LIBPATHIME_BUILD_DEMO}")
message(STATUS "")
