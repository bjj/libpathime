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
#   conf file the anthy port generates names the *install* path, which does not
#   exist in a build tree, so anthy_init() returns -1 and pathime_has_engine()
#   is false. The vendored suite solves this by calling anthy_conf_override()
#   before anthy_init() (tests/anthy/anthy_test_util.h); this test cannot,
#   because those calls would have to reach the copy of anthy inside libpathime
#   and on Windows there is no route to it — see "Why not anthy_conf_override()"
#   below. Instead it is given a second, build-tree conf file through the
#   CONFFILE environment variable: anthy_conf_get_str() falls back to getenv()
#   for names nothing has set, and nothing sets CONFFILE before the first
#   lookup, so the environment decides which conf file is read and that file
#   decides DIC_FILE.
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
#
# ---------------------------------------------------------------------------
# Why not anthy_conf_override(): one anthy per module on Windows
# ---------------------------------------------------------------------------
#
# api.engine_anthy used to link anthy-unicode and make the same four
# anthy_conf_override() calls the vendored suite makes. That worked on Linux
# and silently could not work on Windows, and the difference is not anthy's —
# it is linkage.
#
# anthy is built STATIC on Windows whatever BUILD_SHARED_LIBS says, for reasons
# cmake/ports/anthy-unicode/CMakeLists.txt sets out. libpathime is shared, so
# it absorbs a private copy of anthy, and anthy's configuration lives in
# *process-global* state — the conf database in src-diclib/conf.c, plus the
# initialised flags in src-main/main.c and src-worddic/word_dic.c. A test
# executable that also links anthy therefore gets a *second* copy with its own
# conf database. Its overrides land there; libpathime's anthy_init() runs
# against the other one, finds no dictionary, and pathime_has_engine() answers
# false. (On ELF the two resolve to a single definition, which is exactly why
# the same code passes on Linux — nothing about the test was ever right, it was
# being rescued by symbol interposition.)
#
# The rule this leaves: nothing under tests/api/ may link a backend library.
# These are API-surface tests and the public header is the whole interface they
# are entitled to; where a backend needs pointing at build-tree data, it has to
# be done the way an installation would do it — through the environment and the
# backend's own config files, as below. tests/anthy/ is unaffected: those
# programs link anthy and nothing else, so they have exactly one copy.

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

  # The data directory this run is confined to: pathime_init_params_t::data_dir
  # below, and the HOME of last resort in the environment. Ours alone, so a run
  # starts from clean learning state.
  set(_anthy_home "${CMAKE_CURRENT_BINARY_DIR}/home/api_engine_anthy")
  file(MAKE_DIRECTORY "${_anthy_home}")

  # The build-tree conf file, written here rather than reusing the port's
  # ${ANTHY_TEST_CONF_FILE}: that one is generated from the submodule's
  # anthy-unicode.conf.in for the *codegen tools* and names install paths, and
  # the one thing this test needs is a DIC_FILE that exists now. DIC_FILE is
  # the only key anthy_init() reads (src-diclib/file_dic.c:65); ANTHYDIR and
  # ZIPDICT_EUC are reached only by rule parsing and the zipcode extension,
  # neither of which a built anthy.dic goes through.
  #
  # NEWLINE_STYLE UNIX for the same reason the port uses it: anthy's line
  # parser is LF-oriented and would keep the CR of a CRLF file as part of the
  # path token. Nor may the path contain a space — read_conf_file() splits on
  # "%s %s" — which is a constraint on where the build tree may live, and one
  # the port's own generated conf already carries.
  set(_anthy_conf "${CMAKE_CURRENT_BINARY_DIR}/anthy-buildtree.conf")
  file(CONFIGURE OUTPUT "${_anthy_conf}" NEWLINE_STYLE UNIX CONTENT
"# Generated by tests/api/CMakeLists.txt. Points anthy at the dictionary this
# build produced; api.engine_anthy names it through the CONFFILE environment
# variable, which is the only handle it has on a copy of anthy it does not link.
DIC_FILE ${ANTHY_TEST_DIC_FILE}
")

  target_compile_definitions(api_engine_anthy_test PRIVATE
    "ANTHY_TEST_HOME=\"${_anthy_home}\"")
  add_dependencies(api_engine_anthy_test anthy-dic)

  # CONFFILE is what makes anthy_init() succeed; the other two are belt and
  # braces around it. anthy_get_user_dir() prefers XDG_CONFIG_HOME over HOME,
  # and conf lookups fall back to getenv() for anything nothing overrode —
  # clearing one and redirecting the other means that even a path this test
  # does not know about cannot reach the developer's real ~/.config/anthy.
  # (In practice pathime_init()'s data_dir gets there first: it overrides
  # XDG_CONFIG_HOME inside the library, which wins over both.)
  set_tests_properties(api.engine_anthy PROPERTIES
    ENVIRONMENT "CONFFILE=${_anthy_conf};XDG_CONFIG_HOME=;HOME=${_anthy_home}")

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
    # Without the database there is nothing for this test to convert, so every
    # assertion in it would fail for a reason that has nothing to do with
    # libpathime. Withholding PYZY_TEST_DATA_DIR is what makes the test compile
    # to a pt_skip() instead — the same protocol the PATHIME_WITH_* macros use,
    # one level finer. (The engine now reports itself absent in that situation
    # rather than silently converting nothing; api.engine_pyzy_nodb below is
    # what asserts that, and it is the positive test for this same condition.)
    message(WARNING
      "tests/api: android.db was not built, so api.engine_pyzy has no database "
      "to open and will report itself skipped. It needs Python 3 at configure "
      "time; see PYZY_BUILD_DB_ANDROID.")
  endif()
endif()

# --- api.engine_pyzy_nodb: the negative half --------------------------------
#
# Asserts that a pyzy with no database reports itself unavailable instead of
# pretending to work. Registered conditionally, because its assertion is only
# true when there really is no database to find, and that is a property of the
# machine rather than of this build: pyzy's Database::open() tries three files
# under its compiled-in PKGDATADIR before falling back to "main.db" in the
# working directory (Database.cc:247-252). The working directory is ours to
# control and we simply never stage one; PKGDATADIR is not, so it is probed
# here and the test is skipped entirely when a system-wide pyzy is installed.
#
# The probe duplicates the candidate list in pyzy_database_present(), which is
# a real cost — two places to touch if pyzy's list ever changes. It is accepted
# because the alternative is worse: a test that silently passes for the wrong
# reason on a developer machine with pyzy installed.
if(LIBPATHIME_WITH_PYZY)
  set(_pyzy_system_db_found FALSE)
  foreach(_db_name local.db open-phrase.db android.db)
    if(EXISTS "${CMAKE_INSTALL_FULL_DATADIR}/pyzy/db/${_db_name}")
      set(_pyzy_system_db_found TRUE)
    endif()
  endforeach()

  add_executable(api_engine_pyzy_nodb_test engine_pyzy_nodb_test.c)
  target_link_libraries(api_engine_pyzy_nodb_test PRIVATE libpathime::pathime)
  target_include_directories(api_engine_pyzy_nodb_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}")

  if(_pyzy_system_db_found)
    message(STATUS
      "tests/api: a pyzy database exists under ${CMAKE_INSTALL_FULL_DATADIR}/pyzy/db, "
      "so api.engine_pyzy_nodb is not registered — the condition it asserts "
      "does not hold on this machine.")
  else()
    # Ours alone, and deliberately empty: no main.db is ever staged here, which
    # is the whole of what makes the test's premise true. Kept apart from
    # api.engine_pyzy's rundir for exactly that reason.
    set(_pyzy_nodb_rundir "${CMAKE_CURRENT_BINARY_DIR}/run/engine_pyzy_nodb")
    file(MAKE_DIRECTORY "${_pyzy_nodb_rundir}")

    target_compile_definitions(api_engine_pyzy_nodb_test PRIVATE
      "PATHIME_NODB_DATA_DIR=\"${_pyzy_nodb_rundir}/data\"")

    add_test(NAME api.engine_pyzy_nodb COMMAND api_engine_pyzy_nodb_test)
    set_tests_properties(api.engine_pyzy_nodb PROPERTIES
      SKIP_RETURN_CODE 77
      WORKING_DIRECTORY "${_pyzy_nodb_rundir}")
  endif()
endif()
