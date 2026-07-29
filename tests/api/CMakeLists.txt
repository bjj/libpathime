# API-surface tests, ctest names `api.<name>`.
#
# Peer of the backend directories with the same ground rules — plain C11, no
# external test framework, identical on Linux and Windows — but with one extra
# job: being C is part of the point. These are the only programs in the tree
# that consume <pathime/pathime.h> the way a client does, so they double as
# proof the header is usable from strict C and that every symbol they touch is
# actually exported (which a C++ consumer, or a static build, would not prove).
#
# Most of this directory is ungated: it needs only the libpathime target, which
# always builds. The two end-to-end engine tests are the exception, and the
# reason is *data*, not code. api.engine_anthy and api.engine_pyzy drive a real
# backend all the way to real Japanese and Chinese output, so they need that
# backend's dictionary — which exists only when LIBPATHIME_WITH_ANTHY /
# LIBPATHIME_WITH_PYZY built it. Every test is still registered
# unconditionally, so a registration can never be lost by a configuration
# change; the `.c` file compiles to a pt_skip() when its PATHIME_WITH_* macro is
# 0, and only the extra define lives inside the corresponding if().
# api.engine_hangul needs no such wiring because libhangul carries its tables in
# the library itself.
#
# Skip protocol: a test whose subject is not implemented yet exits
# LIBPATHIME_TEST_SKIP (77), which ctest reports as skipped rather than passed
# or failed. As implementation lands, replace the skip with real checks —
# never delete the registration.
#
# ---------------------------------------------------------------------------
# Runtime data
# ---------------------------------------------------------------------------
#
# Nothing here stages a dictionary or a database, and nothing here sets an
# environment variable or a working directory. src/CMakeLists.txt puts the data
# in the pathime-data/ beside the library, which is where
# pathime_init_params_t::resource_dir looks by default, so these programs find
# it the same way a client's would.
#
# What they do supply is data_dir: each engine test gets its own directory
# under the build tree, so a run never touches the developer's real
# ~/.config/anthy or pyzy user database and each run starts from clean learning
# state.
#
# ---------------------------------------------------------------------------
# One rule: nothing under tests/api/ may link a backend library
# ---------------------------------------------------------------------------
#
# These are API-surface tests, and the public header is the whole interface
# they are entitled to — if something cannot be arranged through
# <pathime/pathime.h>, that is a finding about the header, not a reason to
# reach around it.
#
# A test that reached past the header would also stop being evidence about the
# thing it is named for: that the header is complete, and that every symbol a
# client touches is actually exported.
#
# tests/anthy/ and tests/pyzy/ are where a backend's own surface gets driven
# directly, and they link that backend rather than libpathime.

function(libpathime_add_api_test name source)
  add_executable(api_${name}_test "${source}")
  target_link_libraries(api_${name}_test PRIVATE libpathime::pathime)
  target_include_directories(api_${name}_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  add_test(NAME api.${name} COMMAND api_${name}_test)
  set_tests_properties(api.${name} PROPERTIES SKIP_RETURN_CODE 77)
endfunction()

libpathime_add_api_test(abi abi_test.c)
libpathime_add_api_test(lifecycle lifecycle_test.c)
libpathime_add_api_test(options options_test.c)
libpathime_add_api_test(engine_hangul engine_hangul_test.c)
libpathime_add_api_test(engine_anthy engine_anthy_test.c)
libpathime_add_api_test(engine_pyzy engine_pyzy_test.c)
libpathime_add_api_test(engine_table engine_table_test.c)

# --- api.engine_table --------------------------------------------------------
# Depends on the compiled tables being staged beside the library, which
# libpathime_stage_runtime_data() has already arranged; the test resolves them
# through the default resource directory exactly as a client does, so there is
# no path to pass in.
#
# It does need a data directory of its own, for the same reason api.engine_anthy
# and api.engine_pyzy do: the test commits, two of the shipped tables declare
# DYNAMIC_ADJUST, and learning writes a user database. Wiping it before every
# run is what makes the candidate order the test asserts a property of the
# table rather than of whatever the previous run taught it.
if(LIBPATHIME_WITH_TABLE)
  set(_table_home "${CMAKE_CURRENT_BINARY_DIR}/home/api_engine_table")
  file(MAKE_DIRECTORY "${_table_home}")

  target_compile_definitions(api_engine_table_test PRIVATE
    "TABLE_TEST_DATA_DIR=\"${_table_home}\"")

  add_test(NAME api.engine_table.clean
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${_table_home}/table")
  set_tests_properties(api.engine_table.clean PROPERTIES
    FIXTURES_SETUP api_engine_table_home)
  set_tests_properties(api.engine_table PROPERTIES
    FIXTURES_REQUIRED api_engine_table_home)
endif()

# --- api.engine_anthy --------------------------------------------------------
if(LIBPATHIME_WITH_ANTHY)
  # The data directory this run is confined to. Ours alone, so a run starts
  # from clean learning state and never reaches the developer's own.
  set(_anthy_home "${CMAKE_CURRENT_BINARY_DIR}/home/api_engine_anthy")
  file(MAKE_DIRECTORY "${_anthy_home}")

  target_compile_definitions(api_engine_anthy_test PRIVATE
    "ANTHY_TEST_HOME=\"${_anthy_home}\"")

  # anthy *learns* on commit, and this test commits. The learned record file
  # lives at <home>/anthy/, so wiping that subdirectory before every run is
  # what makes the run reproducible rather than graded against whatever the
  # previous run taught it. A CTest fixture rather than a build step, because
  # the file is produced by the test itself. (The test still avoids asserting
  # any ordering deep enough for learning to reach, which is the belt to this
  # fixture's braces.)
  add_test(NAME api.engine_anthy.clean
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${_anthy_home}/anthy")
  set_tests_properties(api.engine_anthy.clean PROPERTIES
    FIXTURES_SETUP api_engine_anthy_home)
  set_tests_properties(api.engine_anthy PROPERTIES
    FIXTURES_REQUIRED api_engine_anthy_home)
endif()

# --- api.engine_pyzy ---------------------------------------------------------
if(LIBPATHIME_WITH_PYZY)
  if(PYZY_ANDROID_DB)
    set(_pyzy_data "${CMAKE_CURRENT_BINARY_DIR}/home/api_engine_pyzy")
    file(MAKE_DIRECTORY "${_pyzy_data}")

    target_compile_definitions(api_engine_pyzy_test PRIVATE
      "PYZY_TEST_DATA_DIR=\"${_pyzy_data}\"")

    # pyzy learns on commit too, into <data_dir>/pyzy/cache/user-*.db, and this
    # test commits. Same reasoning as api.engine_anthy.clean above.
    add_test(NAME api.engine_pyzy.clean
      COMMAND ${CMAKE_COMMAND} -E rm -rf "${_pyzy_data}/pyzy")
    set_tests_properties(api.engine_pyzy.clean PROPERTIES
      FIXTURES_SETUP api_engine_pyzy_data)
    set_tests_properties(api.engine_pyzy PROPERTIES
      FIXTURES_REQUIRED api_engine_pyzy_data)
  else()
    # Without the database there is nothing for this test to convert, so every
    # assertion in it would fail for a reason that has nothing to do with
    # libpathime. Withholding PYZY_TEST_DATA_DIR is what makes the test compile
    # to a pt_skip() instead — the same protocol the PATHIME_WITH_* macros use,
    # one level finer. (The engine reports itself absent in that situation
    # rather than silently converting nothing; api.engine_pyzy_nodb below is
    # the positive test for this same condition.)
    message(WARNING
      "tests/api: android.db was not built, so api.engine_pyzy has no database "
      "to open and will report itself skipped. It needs Python 3 at configure "
      "time; see PYZY_BUILD_DB_ANDROID.")
  endif()
endif()

# --- api.engine_pyzy_nodb: the negative half --------------------------------
#
# Asserts that a pyzy with no database reports itself unavailable instead of
# pretending to work. Its premise is arranged rather than hoped for: the test
# passes a resource_dir of its own, and nothing ever puts a database there. No
# part of the answer comes from the machine, so this is registered
# unconditionally like everything else here.
if(LIBPATHIME_WITH_PYZY)
  add_executable(api_engine_pyzy_nodb_test engine_pyzy_nodb_test.c)
  target_link_libraries(api_engine_pyzy_nodb_test PRIVATE libpathime::pathime)
  target_include_directories(api_engine_pyzy_nodb_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}")

  set(_pyzy_nodb "${CMAKE_CURRENT_BINARY_DIR}/home/api_engine_pyzy_nodb")
  file(MAKE_DIRECTORY "${_pyzy_nodb}/data")
  file(MAKE_DIRECTORY "${_pyzy_nodb}/resources")

  target_compile_definitions(api_engine_pyzy_nodb_test PRIVATE
    "PATHIME_NODB_DATA_DIR=\"${_pyzy_nodb}/data\""
    "PATHIME_NODB_RESOURCE_DIR=\"${_pyzy_nodb}/resources\"")

  add_test(NAME api.engine_pyzy_nodb COMMAND api_engine_pyzy_nodb_test)
  set_tests_properties(api.engine_pyzy_nodb PROPERTIES SKIP_RETURN_CODE 77)
endif()
