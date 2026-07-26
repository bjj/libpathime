/*
 * End-to-end kana-kanji conversion through the public API, plus a
 * non-degeneracy check on the dictionary the build just produced.
 *
 * Why this exists: on Windows, everything between the input string and the
 * result passes through code that had to be fixed for the platform — the
 * dictionary was written by codegen tools running with the CRT in binary mode,
 * it is read back through the mmap shim in cmake/compat/win32, and every
 * lookup allocates from the slab allocator whose pointer arithmetic had to be
 * widened for LLP64. None of that is exercised by merely linking the library,
 * and a dictionary that loads but yields nothing looks identical to a working
 * one from the outside. A conversion that produces the right kanji proves the
 * whole path.
 */

#include "anthy_test_util.h"

/* にほんごのへんかん -> 日本語の + 変換 */
#define YOMI_NIHONGO   "にほんごのへんかん"
#define KANJI_NIHONGO0 "日本語の"
#define KANJI_NIHONGO1 "変換"

static anthy_context_t
new_context(void)
{
  anthy_context_t ac = anthy_create_context();
  if (!ac) {
    AT_FAILF("anthy_create_context() returned NULL");
    exit(1);
  }
  /* This fork is UTF-8 capable but still defaults to EUC-JP, so every context
   * must be switched over explicitly before any string crosses the API. */
  anthy_context_set_encoding(ac, ANTHY_UTF8_ENCODING);
  return ac;
}

/* Fetch one candidate into a caller-supplied buffer, checking the two-call
 * length protocol agrees with what actually gets written. */
static const char *
get_segment(anthy_context_t ac, int seg, int cand, char *buf, int len)
{
  int need = anthy_get_segment(ac, seg, cand, NULL, 0);
  int got;
  if (need < 0 || need >= len) {
    AT_FAILF("segment %d candidate %d: bad length %d", seg, cand, need);
    return NULL;
  }
  got = anthy_get_segment(ac, seg, cand, buf, len);
  if (got != need)
    AT_FAILF("segment %d candidate %d: length %d on query, %d on fetch",
             seg, cand, need, got);
  if ((int)strlen(buf) != got)
    AT_FAILF("segment %d candidate %d: returned %d but wrote %d bytes",
             seg, cand, got, (int)strlen(buf));
  return buf;
}

/* The conversion this whole port exists to get right. */
static void
test_known_conversion(void)
{
  anthy_context_t ac = new_context();
  struct anthy_conv_stat cs;
  char buf[256];

  AT_CHECK_INT(anthy_set_string(ac, YOMI_NIHONGO), 0);
  AT_CHECK_INT(anthy_get_stat(ac, &cs), 0);
  AT_CHECK_INT(cs.nr_segment, 2);

  if (cs.nr_segment == 2) {
    AT_CHECK_STR(get_segment(ac, 0, 0, buf, sizeof buf), KANJI_NIHONGO0);
    AT_CHECK_STR(get_segment(ac, 1, 0, buf, sizeof buf), KANJI_NIHONGO1);
  }
  anthy_release_context(ac);
}

/*
 * The pseudo-candidates are generated from the reading rather than looked up,
 * so they must hold whatever the dictionary contains. They are also the
 * cheapest possible check that the reading survived the round trip through
 * anthy's internal xstr representation intact.
 */
static void
test_pseudo_candidates(void)
{
  anthy_context_t ac = new_context();
  char buf[256];

  anthy_set_string(ac, "にほんご");
  AT_CHECK_STR(get_segment(ac, 0, NTH_UNCONVERTED_CANDIDATE, buf, sizeof buf),
               "にほんご");
  AT_CHECK_STR(get_segment(ac, 0, NTH_HIRAGANA_CANDIDATE, buf, sizeof buf),
               "にほんご");
  AT_CHECK_STR(get_segment(ac, 0, NTH_KATAKANA_CANDIDATE, buf, sizeof buf),
               "ニホンゴ");
  /* Half-width katakana has no precomposed voiced forms, so ゴ must come back
   * as the two code points ｺ + ﾞ. */
  AT_CHECK_STR(get_segment(ac, 0, NTH_HALFKANA_CANDIDATE, buf, sizeof buf),
               "ﾆﾎﾝｺﾞ");
  anthy_release_context(ac);
}

/*
 * Dictionary integrity. A dictionary that mmaps but contains nothing still
 * converts: anthy falls back to returning the reading itself, so every
 * assertion about "some candidate exists" passes. What an empty dictionary
 * cannot do is produce a candidate that is not just the input back again.
 * That is what this checks, over enough readings that a single unlucky word
 * cannot mask a general failure.
 */
static void
test_dictionary_not_degenerate(void)
{
  static const char *const readings[] = {
    "にほん",       /* 日本 */
    "とうきょう",   /* 東京 */
    "かんじ",       /* 漢字 */
    "じしょ",       /* 辞書 */
    "がくせい",     /* 学生 */
    "でんわ",       /* 電話 */
    "しんぶん",     /* 新聞 */
    "せんせい",     /* 先生 */
  };
  const size_t n = sizeof readings / sizeof readings[0];
  size_t i;
  int converted = 0;
  anthy_context_t ac = new_context();

  for (i = 0; i < n; i++) {
    struct anthy_conv_stat cs;
    struct anthy_segment_stat ss;
    char buf[256];
    int c, distinct = 0;

    anthy_set_string(ac, readings[i]);
    if (anthy_get_stat(ac, &cs) != 0 || cs.nr_segment < 1) {
      AT_FAILF("\"%s\": no segments", readings[i]);
      continue;
    }
    /* A single-word reading that splits is a splitter regression, not a
     * dictionary one, but it is worth knowing about either way. */
    AT_CHECK_INT(cs.nr_segment, 1);

    if (anthy_get_segment_stat(ac, 0, &ss) != 0) {
      AT_FAILF("\"%s\": anthy_get_segment_stat failed", readings[i]);
      continue;
    }
    if (ss.nr_candidate < 2) {
      AT_FAILF("\"%s\": only %d candidate(s)", readings[i], ss.nr_candidate);
      continue;
    }
    /* seg_len counts characters, not bytes: every reading here is
     * three-bytes-per-character hiragana. */
    AT_CHECK_INT(ss.seg_len, (int)strlen(readings[i]) / 3);

    for (c = 0; c < ss.nr_candidate; c++) {
      if (!get_segment(ac, 0, c, buf, sizeof buf))
        break;
      if (strcmp(buf, readings[i]) != 0)
        distinct++;
    }
    if (distinct == 0)
      AT_FAILF("\"%s\": every candidate is the reading itself — the "
               "dictionary looks empty", readings[i]);
    else
      converted++;
    anthy_reset_context(ac);
  }

  AT_CHECK_INT(converted, (int)n);
  anthy_release_context(ac);
}

int
main(void)
{
  at_check_source_encoding();
  at_point_at_build_tree();
  if (anthy_init()) {
    fprintf(stderr, "anthy_init() failed — dictionary %s\n", ANTHY_TEST_DIC);
    return 1;
  }

  test_known_conversion();
  test_pseudo_candidates();
  test_dictionary_not_degenerate();

  anthy_quit();
  return at_report("test_convert");
}
