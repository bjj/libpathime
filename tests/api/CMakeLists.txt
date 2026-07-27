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
# 0, and only the extra include/define/working-directory wiring lives inside
# the corresponding if(). api.engine_hangul needs no such wiring because
# libhangul carries its tables in the library itself.
#
# Skip protocol: a test whose subject is not implemented yet exits
# LIBPATHIME_TEST_SKIP (77), which ctest reports as skipped rather than passed
# or failed. As implementation lands, replace the skip with real checks —
# never delete the registration.
#
# ---------------------------------------------------------------------------
# Runtime data: a test-environment problem, not a library one
# ---------------------------------------------------------------------------
#
# Read this before "fixing" anything in src/ because these tests would not
# otherwise run. In a real installation both backends find their own data and
# nothing below is needed:
#
#   anthy reads CONFFILE at anthy_init() time and takes DIC_FILE from it. The
#   conf file this build generates names the *install* path, which does not
#   exist in a build tree, so anthy_init() returns -1 and pathime_has_engine()
#   is false. anthy_conf_get_str() does fall back to getenv(), but only for
#   names the conf file did not set — and it sets DIC_FILE — so environment
#   variables alone cannot redirect it. The vendored suite solves this with
#   anthy_conf_override() (tests/anthy/anthy_test_util.h) and so does
#   engine_anthy_test.c, which is why that one API test compiles against
#   <anthy/anthy.h>.
#
#   pyzy's Database::open() searches PKGDATADIR — again an install path — and
#   then "main.db" relative to the process's working directory. There is no
#   API to point it anywhere else, so the database is staged into a per-test
#   run directory and the test is given that as its WORKING_DIRECTORY. A run
#   without it does not fail loudly: pyzy warns on stderr, reports success
#   anyway (Database::init() builds the singleton whether or not open()
#   succeeded), and then produces zero candidates — so a test that skipped this
#   would pass vacuously.
#
# Both tests are additionally given their own data directory under the build
# tree, via pathime_init_params_t::data_dir, so a run never touches the
# developer's real ~/.config/anthy or pyzy user database and each run starts
# from clean learning state.

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

# --- api.engine_anthy: point anthy at the dictionary this build produced -----
if(LIBPATHIME_WITH_ANTHY)
  if(NOT TARGET anthy-dic)
    message(FATAL_ERROR
      "tests/api: api.engine_anthy needs the dictionary target from "
      "cmake/ports/anthy-unicode.")
  endif()

  # The same three defines tests/anthy/CMakeLists.txt passes its own programs,
  # under the same names, so the two places stay comparable. The conf file and
  # dictionary are absolute paths into the build tree; the HOME is ours alone.
  set(_anthy_home "${CMAKE_CURRENT_BINARY_DIR}/home/api_engine_anthy")
  file(MAKE_DIRECTORY "${_anthy_home}")

  # <anthy/anthy.h> for anthy_conf_override(), which must be called before
  # pathime_init() — the first conf lookup reads CONFFILE and then refuses to
  # run again. The library target already links anthy, but naming it here keeps
  # the test linkable in a shared build too, where anthy's symbols are not
  # re-exported from libpathime.
  target_include_directories(api_engine_anthy_test PRIVATE "${ANTHY_TEST_SRC_DIR}")
  target_link_libraries(api_engine_anthy_test PRIVATE anthy-unicode)
  target_compile_definitions(api_engine_anthy_test PRIVATE
    "ANTHY_TEST_CONF=\"${ANTHY_TEST_CONF_FILE}\""
    "ANTHY_TEST_DIC=\"${ANTHY_TEST_DIC_FILE}\""
    "ANTHY_TEST_HOME=\"${_anthy_home}\"")
  add_dependencies(api_engine_anthy_test anthy-dic)

  # Belt and braces around the HOME the test also sets through
  # anthy_conf_override(): anthy_get_user_dir() prefers XDG_CONFIG_HOME, and
  # conf lookups fall back to getenv() for anything nothing overrode. Clearing
  # it here means that even a path this test does not know about cannot reach
  # the developer's real ~/.config/anthy.
  set_tests_properties(api.engine_anthy PROPERTIES
    ENVIRONMENT "XDG_CONFIG_HOME=;HOME=${_anthy_home}")

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

# --- api.engine_pyzy: stage main.db into the test's working directory --------
if(LIBPATHIME_WITH_PYZY)
  if(PYZY_ANDROID_DB)
    # Its own directory, not shared with tests/pyzy's: android.db is opened
    # with PRAGMA locking_mode=EXCLUSIVE, so two tests running under `ctest -j`
    # must not be pointed at one file.
    set(_pyzy_rundir "${CMAKE_CURRENT_BINARY_DIR}/run/engine_pyzy")
    file(MAKE_DIRECTORY "${_pyzy_rundir}")

    # pathime_init_params_t::data_dir, absolute so it does not depend on the
    # working directory the way main.db unavoidably does.
    target_compile_definitions(api_engine_pyzy_test PRIVATE
      "PYZY_TEST_DATA_DIR=\"${_pyzy_rundir}/data\"")

    add_dependencies(api_engine_pyzy_test pyzy-db)
    add_custom_command(TARGET api_engine_pyzy_test POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${PYZY_ANDROID_DB}" "${_pyzy_rundir}/main.db"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${CMAKE_SOURCE_DIR}/pyzy/src/phrases.txt" "${_pyzy_rundir}/phrases.txt"
      COMMENT "api.engine_pyzy: staging android.db and phrases.txt"
      VERBATIM)

    set_tests_properties(api.engine_pyzy PROPERTIES
      WORKING_DIRECTORY "${_pyzy_rundir}")

    # pyzy learns on commit too, into <data_dir>/pyzy/cache/user-*.db, and this
    # test commits. Same reasoning as api.engine_anthy.clean above.
    add_test(NAME api.engine_pyzy.clean
      COMMAND ${CMAKE_COMMAND} -E rm -rf "${_pyzy_rundir}/data")
    set_tests_properties(api.engine_pyzy.clean PROPERTIES
      FIXTURES_SETUP api_engine_pyzy_data)
    set_tests_properties(api.engine_pyzy PROPERTIES
      FIXTURES_REQUIRED api_engine_pyzy_data)
  else()
    # Without the database pyzy still reports success and simply produces no
    # candidates, so every assertion in the test would fail for a reason that
    # has nothing to do with libpathime. Withholding PYZY_TEST_DATA_DIR is what
    # makes the test compile to a pt_skip() instead — the same protocol the
    # PATHIME_WITH_* macros use, one level finer.
    message(WARNING
      "tests/api: android.db was not built, so api.engine_pyzy has no database "
      "to open and will report itself skipped. It needs Python 3 at configure "
      "time; see PYZY_BUILD_DB_ANDROID.")
  endif()
endif()
