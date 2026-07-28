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

  set(${sources_var} "${_sources}" PARENT_SCOPE)
  set(${destinations_var} "${_destinations}" PARENT_SCOPE)
endfunction()

# libpathime_install_runtime_data()
#
# Installs the data beside the installed libpathime, which is what makes the
# default resource_dir correct for an installed tree: the library goes to
# CMAKE_INSTALL_LIBDIR (or BINDIR for a Windows DLL), so the data goes into
# `pathime-data` under the same one.
function(libpathime_install_runtime_data)
  if(WIN32 AND BUILD_SHARED_LIBS)
    set(_libdir "${CMAKE_INSTALL_BINDIR}")
  else()
    set(_libdir "${CMAKE_INSTALL_LIBDIR}")
  endif()

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
      DESTINATION "${_libdir}/${LIBPATHIME_RUNTIME_DATA_DIRNAME}/${_subdir}"
      RENAME "${_name}")
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
  # dependency has to be on those targets and not just on the paths.
  if(LIBPATHIME_WITH_ANTHY)
    add_dependencies(pathime-runtime-data anthy-dic)
  endif()
  if(LIBPATHIME_WITH_PYZY AND PYZY_ANDROID_DB)
    add_dependencies(pathime-runtime-data pyzy-db)
  endif()
endfunction()
