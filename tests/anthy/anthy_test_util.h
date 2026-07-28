/*
 * Shared scaffolding for the libpathime-authored anthy tests.
 *
 * Deliberately not a framework: a counter, three assertion macros and the
 * conf-override boilerplate every test needs to be pointed at the build tree
 * instead of an installed anthy. Each test is one translation unit, so all of
 * this is static.
 */
#ifndef LIBPATHIME_ANTHY_TEST_UTIL_H
#define LIBPATHIME_ANTHY_TEST_UTIL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <anthy/anthy.h>

/* The build system must tell us where the freshly built dictionary is and give
 * us a scratch HOME. Without them a test would silently fall back to whatever
 * anthy is installed on the machine — which is exactly the failure mode these
 * tests exist to rule out. */
#if !defined(ANTHY_TEST_DIC) || !defined(ANTHY_TEST_HOME)
# error "ANTHY_TEST_DIC and ANTHY_TEST_HOME must be defined"
#endif

static int at_failures;

#define AT_FAILF(...)                                                         \
  do {                                                                        \
    fprintf(stderr, "%s:%d: FAIL: ", __FILE__, __LINE__);                     \
    fprintf(stderr, __VA_ARGS__);                                             \
    fputc('\n', stderr);                                                      \
    at_failures++;                                                            \
  } while (0)

#define AT_CHECK(cond)                                                        \
  do {                                                                        \
    if (!(cond)) AT_FAILF("%s", #cond);                                       \
  } while (0)

/* Reports both sides on mismatch: for Japanese text the interesting failure is
 * usually "got mojibake", which a bare boolean would not show. */
#define AT_CHECK_STR(got, want)                                               \
  do {                                                                        \
    const char *at_g = (got), *at_w = (want);                                 \
    if (!at_g || strcmp(at_g, at_w) != 0)                                     \
      AT_FAILF("expected \"%s\", got \"%s\"", at_w, at_g ? at_g : "(null)");  \
  } while (0)

#define AT_CHECK_INT(got, want)                                               \
  do {                                                                        \
    long at_g = (long)(got), at_w = (long)(want);                             \
    if (at_g != at_w) AT_FAILF("%s: expected %ld, got %ld", #got, at_w, at_g);\
  } while (0)

static void
at_log(int level, const char *msg)
{
  /* anthy's own diagnostics go to stderr so a failing test shows them, but
   * they are not failures in themselves — a missing conf file, say, surfaces
   * as a wrong conversion further down. */
  fprintf(stderr, "anthy[%d]: %s", level, msg);
}

/*
 * Point anthy at the build tree. This has to happen before anthy_init(): the
 * first conf lookup triggers anthy_do_conf_init(), which then refuses to run
 * again.
 *
 * An empty CONFFILE says there is no conf file to read, so every value anthy
 * uses is one of the three below and none can come from an installation on
 * this machine. XDG_CONFIG_HOME is given an explicit empty value for the same
 * reason: anthy_get_user_dir() prefers it over HOME and falls through to HOME
 * when it is empty, but a name with no value at all falls back to getenv() —
 * so on a Linux desktop, setting HOME alone would still leave the private
 * dictionary being written to the real ~/.config/anthy.
 */
static void
at_point_at_build_tree(void)
{
  anthy_set_logger(at_log, 0);
  anthy_conf_override("CONFFILE", "");
  anthy_conf_override("DIC_FILE", ANTHY_TEST_DIC);
  anthy_conf_override("HOME", ANTHY_TEST_HOME);
  anthy_conf_override("XDG_CONFIG_HOME", "");
}

/*
 * Guard against the compiler having read this file in the wrong charset.
 * MSVC assumes the system ANSI codepage unless given /utf-8, and the lead
 * bytes of the Japanese literals below (0x81, 0x8d, ...) are unmapped in
 * CP1252 — so a misconfigured build would compare against mangled expectations
 * and could "pass" for the wrong reason. Cheaper to check than to debug.
 */
static void
at_check_source_encoding(void)
{
  static const unsigned char hiragana_a[] = { 0xe3, 0x81, 0x82, 0x00 };
  if (strcmp("あ", (const char *)hiragana_a) != 0)
    AT_FAILF("source was not compiled as UTF-8 (need /utf-8 on MSVC)");
}

static int
at_report(const char *name)
{
  if (at_failures) {
    printf("%s: %d failure(s)\n", name, at_failures);
    return 1;
  }
  printf("%s: ok\n", name);
  return 0;
}

#endif /* LIBPATHIME_ANTHY_TEST_UTIL_H */
