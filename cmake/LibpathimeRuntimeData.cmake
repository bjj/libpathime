# The read-only data libpathime ships, and the directory it lives in.
#
# pathime_init_params_t::resource_dir defaults to a directory named
# `pathime-data` beside the libpathime binary, so a client that keeps the two
# together needs to configure nothing. This module is the build's half of that
# arrangement: it knows which files go in, what they are called once they are
# there, and how to put them there — both in an installed tree and in the build
# tree, so that the demo and the tests find their data the same way a client
# does.
#
# Adding an engine that ships data means adding it to the list in
# libpathime_runtime_data_files() and nothing else.

set(LIBPATHIME_RUNTIME_DATA_DIRNAME "pathime-data")

# The tables compiled into the shipped data, as
#   <name>|<source .txt, relative to engines/ibus-table-chinese>|<freq source or "">
#
# The set covers the distinct table shapes worth exercising, and between
# them they exercise every shape the engine has code for: RULES and
# USER_CAN_DEFINE_PHRASE (wubi-jidian86), AUTO_WILDCARD and char prompts
# (cangjie5, quick5), AUTO_COMMIT without AUTO_SELECT and non-alphabetic
# VALID_INPUT_CHARS (stroke5), a phonetic key set (zhuyin), and a table
# declaring LANGUAGE_FILTER for traditional Chinese (quick5).
#
# Adding a table is one line here. The whole of engines/ibus-table-chinese is
# available — 29 MB of source across thirteen families — but shipping all of it
# would multiply the package for methods no client has asked for, so the default
# is this set and the rest is a one-line opt-in.
#
# The third field is the frequency-transfer source of tools/table-compile. It
# is set for exactly the two families the transfer was designed around (in
# bjj/ibus-table-chinese d0f9849 and cc4a17f, where the technique originates):
# without it, a partially typed Cangjie or Quick code offers the table's
# structural order rather than common characters first.
set(LIBPATHIME_TABLES
  "cangjie5|tables/cangjie/cangjie5.txt|tables/cantonese/cantonese.txt"
  "quick5|tables/quick/quick5.txt|tables/cantonese/cantonese.txt"
  "wubi-jidian86|tables/wubi-jidian/wubi-jidian86.txt|"
  "stroke5|tables/stroke5/stroke5.txt|"
  "zhuyin|tables/zhuyin.txt|"
  CACHE STRING "Tables to compile into pathime-data/table/")

# libpathime_compile_tables()
#
# Declare the commands that compile each entry of LIBPATHIME_TABLES, and publish
# the resulting .db paths as LIBPATHIME_TABLE_OUTPUTS in the caller's scope.
#
# Must be called from the directory whose target will consume the outputs —
# add_custom_command outputs are directory-scoped — which is src/, where
# libpathime_stage_runtime_data() creates the target that depends on them.
function(libpathime_compile_tables)
  set(_outputs "")
  if(NOT LIBPATHIME_WITH_TABLE OR NOT LIBPATHIME_TABLE_DATA)
    set(LIBPATHIME_TABLE_OUTPUTS "" PARENT_SCOPE)
    return()
  endif()

  set(_root "${PROJECT_SOURCE_DIR}/engines/ibus-table-chinese")
  set(_dir "${PROJECT_BINARY_DIR}/tables")

  foreach(_entry ${LIBPATHIME_TABLES})
    string(REPLACE "|" ";" _fields "${_entry}")
    list(GET _fields 0 _name)
    list(GET _fields 1 _source)
    list(LENGTH _fields _field_count)
    set(_freq "")
    if(_field_count GREATER 2)
      list(GET _fields 2 _freq)
    endif()

    set(_source_path "${_root}/${_source}")
    if(NOT EXISTS "${_source_path}")
      message(WARNING "libpathime: table source missing, skipping: ${_source_path}")
      continue()
    endif()

    set(_output "${_dir}/${_name}.db")
    set(_args "")
    set(_depends "${_source_path}" pathime-table-compile)
    if(_freq)
      list(APPEND _args --freq-from "${_root}/${_freq}")
      list(APPEND _depends "${_root}/${_freq}")
    endif()

    # LIBPATHIME_TABLE_COVERAGE=none is honoured here rather than by compiling
    # the map out of coverage.cc, which keeps the map testable whatever a build
    # ships. The other two values reached the tool as a compile definition when
    # it was built; see cmake/LibpathimeOptions.cmake.
    if(LIBPATHIME_TABLE_COVERAGE STREQUAL "none")
      list(APPEND _args --no-glyph-filter)
    endif()

    add_custom_command(
      OUTPUT "${_output}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${_dir}"
      COMMAND $<TARGET_FILE:pathime-table-compile>
              ${_args} "${_source_path}" "${_output}"
      DEPENDS ${_depends}
      COMMENT "libpathime: compiling table ${_name}"
      VERBATIM)
    list(APPEND _outputs "${_output}")
  endforeach()

  # A custom command runs only for a target that consumes its output, and the
  # tables have two consumers that are both conditional: the staging target
  # below, which exists only for the demo and the tests, and an install rule,
  # which is not a target at all and so cannot pull them into the build. Without
  # a target of their own an ordinary build would compile no table and then fail
  # in `cmake --install` looking for one.
  #
  # This target is then the tables' *only* builder, which
  # libpathime_stage_runtime_data() arranges by depending on it. Two targets in
  # one directory that both consume the same custom-command output make the
  # Makefile and Visual Studio generators emit the rule into each of them, and a
  # parallel build then runs two table compilers on one .db file — which fails
  # outright, because compile_table() unlinks the output first and the pair share
  # one SQLite journal. Ninja deduplicates and hides it.
  if(_outputs)
    add_custom_target(pathime-tables ALL DEPENDS ${_outputs})
  endif()

  set(LIBPATHIME_TABLE_OUTPUTS "${_outputs}" PARENT_SCOPE)
endfunction()

# libpathime_runtime_data_files(<sources_var> <destinations_var>)
#
# Two parallel lists: the file each backend's build produced, and its path
# relative to the resource directory. Entries appear only for backends this
# configuration built, so a library without Japanese ships no dictionary.
function(libpathime_runtime_data_files sources_var destinations_var)
  set(_sources "")
  set(_destinations "")

  if(LIBPATHIME_WITH_ANTHY)
    list(APPEND _sources "${ANTHY_DIC_FILE}")
    list(APPEND _destinations "anthy/anthy.dic")
  endif()

  if(LIBPATHIME_WITH_PYZY)
    # android.db is optional — it needs Python 3 at configure time — and the
    # Chinese engines report themselves unavailable without it.
    if(PYZY_ANDROID_DB)
      list(APPEND _sources "${PYZY_ANDROID_DB}")
      list(APPEND _destinations "pyzy/main.db")
    endif()
    list(APPEND _sources "${PYZY_PHRASES_FILE}")
    list(APPEND _destinations "pyzy/phrases.txt")
  endif()

  # The compiled tables, named in the resource directory the way a client names
  # them in PATHIME_OPT_TABLE_FILE: `table/<name>.db`, so setting the option to
  # "cangjie5" reaches this file.
  foreach(_table ${LIBPATHIME_TABLE_OUTPUTS})
    get_filename_component(_name "${_table}" NAME)
    list(APPEND _sources "${_table}")
    list(APPEND _destinations "table/${_name}")
  endforeach()

  set(${sources_var} "${_sources}" PARENT_SCOPE)
  set(${destinations_var} "${_destinations}" PARENT_SCOPE)
endfunction()

# libpathime_install_runtime_data()
#
# Installs the data beside the module that resolves it, which is what makes the
# default resource_dir correct for an installed tree. Which directory that is —
# CMAKE_INSTALL_LIBDIR beside a shared libpathime, BINDIR for a DLL or a static
# build — is settled once by LIBPATHIME_INSTALL_DATADIR in
# cmake/LibpathimeInstall.cmake, which states the rule and why; the CMake package
# and the .pc file both have to name the same place. libpathime_stage_runtime_data()
# below applies that rule to the build tree, and the two conditions match.
function(libpathime_install_runtime_data)
  libpathime_runtime_data_files(_sources _destinations)
  list(LENGTH _sources _count)
  if(_count EQUAL 0)
    return()   # a build with no backend that ships data
  endif()
  math(EXPR _last "${_count} - 1")
  foreach(_i RANGE 0 ${_last})
    list(GET _sources ${_i} _source)
    list(GET _destinations ${_i} _destination)
    get_filename_component(_subdir "${_destination}" DIRECTORY)
    get_filename_component(_name "${_destination}" NAME)
    # RENAME because the installed name is ours, not the producing build's:
    # pyzy's android.db is what its Database::open() calls main.db.
    install(FILES "${_source}"
      DESTINATION "${LIBPATHIME_INSTALL_DATADIR}/${_subdir}"
      RENAME "${_name}"
      COMPONENT data)
  endforeach()
endfunction()

# libpathime_stage_runtime_data()
#
# The build-tree equivalent, for the demo and the tests: puts the same files
# where a program built here will look for them. Which directory that is
# follows from how the library was linked — beside the shared library every
# program loads, or beside the programs themselves when the library is inside
# them — and both are the directory module_dir() will name at runtime.
#
# The copies are add_custom_command outputs rather than a POST_BUILD step, so
# rebuilding does not re-compare tens of megabytes of dictionary each time.
function(libpathime_stage_runtime_data)
  # Which directory that is, named rather than derived from a target: an
  # add_custom_command OUTPUT has to be a path CMake can know at configure
  # time, so $<TARGET_FILE_DIR:pathime> is not available here. $<CONFIG> is,
  # and it is the one part a multi-config generator adds.
  if(BUILD_SHARED_LIBS AND NOT WIN32)
    set(_base "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")   # beside libpathime.so
  else()
    set(_base "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")   # beside the DLL, or the programs
  endif()
  if(NOT _base)
    message(FATAL_ERROR
      "libpathime: the demo and the tests read their data from beside the "
      "binary that resolves it, so the build has to say where that lands. Set "
      "CMAKE_LIBRARY_OUTPUT_DIRECTORY and CMAKE_RUNTIME_OUTPUT_DIRECTORY.")
  endif()

  get_property(_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
  if(_multi_config)
    set(_base "${_base}/$<CONFIG>")
  endif()
  set(_dir "${_base}/${LIBPATHIME_RUNTIME_DATA_DIRNAME}")

  libpathime_runtime_data_files(_sources _destinations)
  list(LENGTH _sources _count)
  if(_count EQUAL 0)
    return()   # a build with no backend that ships data
  endif()
  math(EXPR _last "${_count} - 1")

  set(_staged "")
  foreach(_i RANGE 0 ${_last})
    list(GET _sources ${_i} _source)
    list(GET _destinations ${_i} _destination)
    add_custom_command(
      OUTPUT "${_dir}/${_destination}"
      COMMAND ${CMAKE_COMMAND} -E copy "${_source}" "${_dir}/${_destination}"
      DEPENDS "${_source}"
      COMMENT "libpathime: staging ${_destination}"
      VERBATIM)
    list(APPEND _staged "${_dir}/${_destination}")
  endforeach()

  add_custom_target(pathime-runtime-data ALL DEPENDS ${_staged})

  # The generated files are outputs of other targets' custom commands, so the
  # dependency has to be on those targets and not just on the paths. The tables
  # are the case where it is load-bearing rather than merely correct: their
  # commands are declared in this same directory, so without the ordering both
  # targets would build them, concurrently — libpathime_compile_tables() above
  # has the detail.
  if(LIBPATHIME_WITH_ANTHY)
    add_dependencies(pathime-runtime-data anthy-dic)
  endif()
  if(LIBPATHIME_WITH_PYZY AND PYZY_ANDROID_DB)
    add_dependencies(pathime-runtime-data pyzy-db)
  endif()
  if(TARGET pathime-tables)
    add_dependencies(pathime-runtime-data pathime-tables)
  endif()
endfunction()
