# API-surface tests, ctest names `api.<name>`.
#
# Peer of the backend directories with the same ground rules — plain C11, no
# external test framework, identical on Linux and Windows — but with one extra
# job: being C is part of the point. These are the only programs in the tree
# that consume <pathime/pathime.h> the way a client does, so they double as
# proof the header is usable from strict C and that every symbol they touch is
# actually exported (which a C++ consumer, or a static build, would not prove).
#
# Unlike the backend directories this one is not gated on a LIBPATHIME_WITH_*
# option: it needs only the libpathime target, which always builds.
#
# Skip protocol: a test whose subject is not implemented yet exits
# LIBPATHIME_TEST_SKIP (77), which ctest reports as skipped rather than passed
# or failed. As implementation lands, replace the skip with real checks —
# never delete the registration.

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
