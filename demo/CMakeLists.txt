# The demo application: an interactive terminal IME, driven through
# <pathime/pathime.h> and nothing else. See demo/README.md for what it shows
# and how to drive it.
#
# Two things this directory is careful about:
#
#   1. It links libpathime::pathime and cpp-terminal, and no backend library.
#      The same rule tests/api/CMakeLists.txt sets out: the public header is
#      the whole interface a client is entitled to, and on Windows linking
#      anthy alongside libpathime is actively broken because anthy is absorbed
#      statically into pathime.dll.
#
#   2. It reaches the backends' runtime data the way an *installation* would —
#      through the environment and each backend's own search path — because in
#      an uninstalled build tree neither anthy nor pyzy can find its data. That
#      is the same test-environment problem tests/api/CMakeLists.txt documents
#      at length, solved the same way and for the same reason. The paths are
#      handed to the program as compile definitions; main.cc applies them only
#      when the corresponding value is defined, so an installed copy of this
#      program does nothing and lets each backend find its own data.

# --- cpp-terminal, the portable terminal layer. Vendored as a submodule under
# this directory, so nothing above demo/ has to know it exists.
#
# Everything but the library itself is turned off: examples and tests would
# land in our build and our ctest run, docs wants Doxygen, and install would
# put a library nobody asked for into the install tree.
#
# Written into the *cache* rather than set as normal variables, which is the
# part worth knowing. cpp-terminal declares these with option(), and its
# cmake_minimum_required(3.10) leaves CMP0077 at OLD inside its directory — so
# option() there clears a normal variable of the same name instead of honouring
# it. A cache entry that already exists is left alone by option() under either
# policy setting, so this is the form that works regardless.
#
# It also FORCEs CMAKE_CXX_STANDARD_REQUIRED and CMAKE_CXX_EXTENSIONS into the
# cache. Both already hold those values from LibpathimeOptions, so the write
# changes nothing — but it is a write into our cache, hence this note.
set(CPPTERMINAL_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CPPTERMINAL_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(CPPTERMINAL_ENABLE_DOCS    OFF CACHE BOOL "" FORCE)
set(CPPTERMINAL_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(cpp-terminal)

# --- Silence the vendored build ---------------------------------------------
#
# A warning in a vendored tree we never edit is not actionable here, and it
# buries what our own code has to say. Two sources of them, silenced two ways —
# both from outside, so the checkout stays pristine, which is the same principle
# that governs the other three submodules.
#
# First, cpp-terminal turns its own warnings up deliberately — `-Wall -Wextra
# -Wpedantic -Wnoexcept …`, and `/Wall` under MSVC — through an INTERFACE target
# that reaches its sources by way of cpp-terminal-private. Emptying that target
# removes exactly those flags from exactly the targets that opted into them.
if(TARGET cpp-terminalWarnings)
  set_target_properties(cpp-terminalWarnings PROPERTIES INTERFACE_COMPILE_OPTIONS "")
endif()

# Second, the compiler's *defaults*, which no flag of theirs turned on and
# clearing that target therefore cannot reach — glibc's warn_unused_result on
# read(2) fires in private/sigwinch.cpp with no -W flags at all. `-w` covers
# those, and is safe here in a way it would not be anywhere else: it is PRIVATE
# on the vendored target, so it can never reach a source of ours.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(cpp-terminal PRIVATE -w)
endif()

# One flag survives all of that, and only under MSVC proper: cpp-terminal's
# top-level CMakeLists also forces `/W4` into its own directory's
# CMAKE_CXX_FLAGS, and a parent scope cannot reach that afterwards. It is left
# alone deliberately — `/w` would win on the command line but make cl print
# "overriding '/W4' with '/w'" for every file, trading a few warnings for a
# confusing one per source. `/W4` on its own is ordinary; `/Wall`, the unusable
# one, is gone with the block above. clang-cl takes the `-w` path instead, since
# that block of theirs tests for CMAKE_CXX_COMPILER_ID STREQUAL MSVC and
# clang-cl reports Clang.

# Its headers are a third party's too, so a build that turns warnings on for our
# code should not start reporting them from <cpp-terminal/*.hpp>. Marking the
# published include directories SYSTEM costs nothing and says so.
get_target_property(_ct_includes cpp-terminal INTERFACE_INCLUDE_DIRECTORIES)
if(_ct_includes)
  set_target_properties(cpp-terminal PROPERTIES
    INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_ct_includes}")
endif()

add_executable(pathime-demo
  src/main.cc
  src/app.cc
  src/keymap.cc
  src/options_view.cc
  src/render.cc
  src/text.cc
)

target_link_libraries(pathime-demo PRIVATE
  libpathime::pathime
  cpp-terminal::cpp-terminal)

target_compile_features(pathime-demo PRIVATE cxx_std_17)
target_include_directories(pathime-demo PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

# MSVC needs telling that these sources are UTF-8; they carry the demo's own
# labels and the box-drawing characters it renders with.
if(MSVC)
  target_compile_options(pathime-demo PRIVATE /utf8)
endif()

# --- Runtime data, for a demo run out of the build tree ----------------------
#
# PATHIME_DEMO_DATA_DIR is where the engines keep what they learn. Under the
# build tree so a demo run never writes to the developer's real ~/.config/anthy
# or pyzy user database — the same isolation the api tests get.
set(_demo_data "${CMAKE_CURRENT_BINARY_DIR}/data")
target_compile_definitions(pathime-demo PRIVATE
  "PATHIME_DEMO_DATA_DIR=\"${_demo_data}\"")

if(LIBPATHIME_WITH_ANTHY)
  # anthy reads CONFFILE at anthy_init() time and takes DIC_FILE from it. The
  # conf file the anthy port generates names the *install* path, which does not
  # exist here, so without this anthy_init() fails and pathime_has_engine()
  # answers false for Japanese. main.cc puts this path in the environment
  # before pathime_init(), which is the only handle a program that does not
  # link anthy has on it — see tests/api/CMakeLists.txt, "Why not
  # anthy_conf_override()".
  #
  # NEWLINE_STYLE UNIX because anthy's line parser is LF-oriented and would
  # keep the CR of a CRLF file as part of the path token. The path may not
  # contain a space either: read_conf_file() splits on "%s %s".
  set(_demo_anthy_conf "${CMAKE_CURRENT_BINARY_DIR}/anthy-buildtree.conf")
  file(CONFIGURE OUTPUT "${_demo_anthy_conf}" NEWLINE_STYLE UNIX CONTENT
"# Generated by demo/CMakeLists.txt. Points anthy at the dictionary this build
# produced; pathime-demo names it through the CONFFILE environment variable.
DIC_FILE ${ANTHY_TEST_DIC_FILE}
")
  target_compile_definitions(pathime-demo PRIVATE
    "PATHIME_DEMO_ANTHY_CONF=\"${_demo_anthy_conf}\"")
  add_dependencies(pathime-demo anthy-dic)
endif()

if(LIBPATHIME_WITH_PYZY)
  if(PYZY_ANDROID_DB)
    # pyzy's Database::open() searches its compiled-in PKGDATADIR — an install
    # path — and then "main.db" relative to the process's working directory.
    # There is no API to point it anywhere else, so the database is staged into
    # a directory of ours and main.cc changes into it at startup. Its own
    # directory, not shared with any test's: android.db is opened with PRAGMA
    # locking_mode=EXCLUSIVE, so two programs must not be pointed at one file.
    set(_demo_pyzy_dir "${CMAKE_CURRENT_BINARY_DIR}/run")
    file(MAKE_DIRECTORY "${_demo_pyzy_dir}")
    target_compile_definitions(pathime-demo PRIVATE
      "PATHIME_DEMO_PYZY_DIR=\"${_demo_pyzy_dir}\"")
    add_dependencies(pathime-demo pyzy-db)
    add_custom_command(TARGET pathime-demo POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${PYZY_ANDROID_DB}" "${_demo_pyzy_dir}/main.db"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${CMAKE_SOURCE_DIR}/pyzy/src/phrases.txt" "${_demo_pyzy_dir}/phrases.txt"
      COMMENT "pathime-demo: staging android.db and phrases.txt"
      VERBATIM)
  else()
    # Not fatal: the demo runs, and reports Chinese unavailable through
    # pathime_has_engine() exactly as it would on a machine with no database.
    message(STATUS
      "demo: android.db was not built, so pathime-demo will report the Chinese "
      "engines unavailable. It needs Python 3 at configure time; see "
      "PYZY_BUILD_DB_ANDROID.")
  endif()
endif()
