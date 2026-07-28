# CTest driver for libhangul's two vendored test programs.
#
# engines/libhangul/test/hangul.c and hanja.c are not self-checking tests: they are
# stdin -> stdout filters written to be driven by hand. To use them as
# regression tests something has to feed them and judge the result, and
# add_test() cannot redirect stdin. This script is that something; it is run
# with `cmake -P`, so it needs no shell and behaves the same under cmd.exe,
# PowerShell and sh.
#
#   cmake -DEXE=<program> -DINPUT=<stdin file> -DEXPECTED=<expected stdout>
#         [-DEXE_ARG=<single extra argv[1]>] -P run_filter_test.cmake
#
# The output is captured to a file rather than to OUTPUT_VARIABLE, which
# matters only on Windows and matters a lot. Both programs emit UTF-8; CMake
# decodes a child's pipe output using the console code page before storing it
# in a variable, so on an OEM-code-page console every Hangul byte comes back as
# a different character and a byte-exact comparison can never succeed. Written
# straight to a file the bytes survive, and file(READ) reads them verbatim.
#
# Line endings are normalised before comparing. Both programs write with
# printf() on a text-mode stream, so every '\n' arrives as CRLF on Windows and
# as LF on Linux. That difference is stdio's, not libhangul's, and holding the
# two platforms to a byte-identical stdout would only test the CRT. Everything
# else -- ordering, content, the UTF-8 bytes themselves -- is compared exactly.

foreach(var EXE INPUT EXPECTED)
  if(NOT DEFINED ${var})
    message(FATAL_ERROR "run_filter_test.cmake: -D${var}= is required")
  endif()
endforeach()

set(_args "")
if(DEFINED EXE_ARG)
  set(_args "${EXE_ARG}")
endif()

if(NOT DEFINED ACTUAL)
  set(ACTUAL "${EXPECTED}.actual")
endif()

execute_process(
  COMMAND "${EXE}" ${_args}
  INPUT_FILE "${INPUT}"
  OUTPUT_FILE "${ACTUAL}"
  ERROR_VARIABLE stderr_text
  RESULT_VARIABLE rc)

if(NOT rc EQUAL 0)
  message(FATAL_ERROR
    "${EXE} exited with ${rc}\n--- stderr ---\n${stderr_text}")
endif()

file(READ "${ACTUAL}" actual)
file(READ "${EXPECTED}" expected)

# Normalise CRLF -> LF, then drop trailing blank lines from both sides so a
# missing or extra final newline is not reported as a content difference.
foreach(var actual expected)
  string(REPLACE "\r\n" "\n" ${var} "${${var}}")
  string(REGEX REPLACE "\n+$" "" ${var} "${${var}}")
endforeach()

if(NOT actual STREQUAL expected)
  message(FATAL_ERROR
    "${EXE}: output does not match ${EXPECTED}\n"
    "(the captured bytes are in ${ACTUAL})\n"
    "--- expected ---\n${expected}\n"
    "--- actual ---\n${actual}\n"
    "--- end ---")
endif()
