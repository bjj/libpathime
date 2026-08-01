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
#   2. It finds the backends' runtime data exactly as a shipped client would,
#      in the pathime-data/ beside the library — so it does nothing at all to
#      arrange that. The one path it is told about is where to keep what the
#      engines learn, which it puts under the build tree rather than in the
#      developer's profile.

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

# cpp-terminal is built *static*, and that is not a preference — a shared build
# of it cannot be linked against on Windows at all.
#
# Its two published globals are plain `extern` references with no dllimport or
# dllexport anywhere: `extern TOstream& cout;` (cpp-terminal/iostream.hpp:19)
# and `extern Term::Terminal& terminal;` (cpp-terminal/terminal.hpp:21). Data
# symbols are the one case CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS does not rescue —
# it can put them in the export table, but a consumer still has to *see*
# __declspec(dllimport) to read them through an import library, and there is
# none to see. So a DLL build links every function and neither of those two,
# and pathime-demo fails with LNK2019 on both.
#
# Upstream knows: its own CMakeLists guards exactly this, defaulting
# BUILD_SHARED_LIBS off under WIN32 with a `##FIXME` beside it
# (cpp-terminal/CMakeLists.txt:35-39). The guard cannot fire here, because it
# is an option() and BUILD_SHARED_LIBS is already a cache entry by then
# (cmake/LibpathimeOptions.cmake:57) — option() leaves an existing cache entry
# alone. That is the same mechanism the block above depends on, working against
# us this time, which is why turning the option ON is what breaks the build.
#
# Forced off across the add_subdirectory and put back immediately after. The
# restore is the load-bearing half: add_subdirectory() runs to completion here,
# so only cpp-terminal's own targets are created while the value is off, but
# leaving it off would silently make libpathime itself static on the next
# configure. Static everywhere rather than only under WIN32, because
# cpp-terminal is the demo's private dependency and the demo is never
# installed — one rule, and the executable runs without a library beside it.
get_property(_ct_bsl_help CACHE BUILD_SHARED_LIBS PROPERTY HELPSTRING)
set(_ct_bsl_saved "${BUILD_SHARED_LIBS}")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "${_ct_bsl_help}" FORCE)

add_subdirectory(cpp-terminal)

set(BUILD_SHARED_LIBS "${_ct_bsl_saved}" CACHE BOOL "${_ct_bsl_help}" FORCE)
unset(_ct_bsl_saved)
unset(_ct_bsl_help)

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
#
# The flag is `/utf-8`, with the hyphen. `/utf8` is not an abbreviation of it —
# cl rejects it outright with "D9002: ignoring unknown option", which is a
# *warning*, so the build went on succeeding while the sources were being read
# in the host ANSI codepage and every non-ASCII literal in demo/src/ came out
# mojibake.
if(MSVC)
  target_compile_options(pathime-demo PRIVATE /utf-8)
endif()

# --- Where the demo keeps what the engines learn -----------------------------
#
# Under the build tree, so a demo run never writes to the developer's real
# ~/.config/anthy or pyzy user database — the same isolation the api tests get.
# --data-dir overrides it.
#
# The directory is created now, at configure time, because main.cc takes the
# baked path only when it exists on disk: that is what lets the same binary,
# unpacked from a demo package on another machine, fall through to the
# library's own per-user default instead of writing to a dead absolute path
# from the machine that built it.
#
# The engines' *read-only* data needs nothing here: it is staged beside the
# library by src/CMakeLists.txt and the default resource_dir finds it, which is
# the same thing that will happen for a client shipping the two together.
set(_demo_data "${CMAKE_CURRENT_BINARY_DIR}/data")
file(MAKE_DIRECTORY "${_demo_data}")
target_compile_definitions(pathime-demo PRIVATE
  "PATHIME_DEMO_DATA_DIR=\"${_demo_data}\"")

if(LIBPATHIME_WITH_PYZY AND NOT PYZY_ANDROID_DB)
  # Not fatal: the demo runs, and reports Chinese unavailable through
  # pathime_has_engine() exactly as it would on a machine with no database.
  message(STATUS
    "demo: android.db was not built, so pathime-demo will report the Chinese "
    "engines unavailable. It needs Python 3 at configure time; see "
    "PYZY_BUILD_DB_ANDROID.")
endif()
