# Script mode: fail if FILE is missing or smaller than MIN_BYTES.
#
# anthy's codegen tools report missing inputs on stdout and still exit 0, so a
# misconfigured stage produces a tiny, structurally valid, useless file rather
# than a build error. The dictionary pipeline runs this after the stages whose
# failure would otherwise be silent.
#
#   cmake -DFILE=<path> -DMIN_BYTES=<n> -DWHAT=<description> -P assert_min_size.cmake

if(NOT EXISTS "${FILE}")
  message(FATAL_ERROR "anthy: ${WHAT} was not produced (${FILE})")
endif()

file(SIZE "${FILE}" _size)
if(_size LESS MIN_BYTES)
  message(FATAL_ERROR
    "anthy: ${WHAT} is only ${_size} bytes (expected at least ${MIN_BYTES}).\n"
    "  ${FILE}\n"
    "  The generating tool exited successfully but read none of its inputs — "
    "check the paths it was given and the anthy-unicode.conf next to them.")
endif()
