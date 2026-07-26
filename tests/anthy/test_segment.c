/*
 * Segment-level operations: resizing, committing, and prediction.
 *
 * These are the paths an IME driver actually drives, and they are the ones
 * that reach furthest into anthy's internals — the splitter's lattice, the
 * ordering library's commit bookkeeping, and (for prediction) the record
 * store under HOME. On Windows all three sit on top of the slab allocator
 * whose pointer arithmetic had to be widened for LLP64, and resizing is what
 * makes the allocator churn: every resize frees and rebuilds the segment list.
 *
 * Upstream's own coverage of this is test/check.c's shake_test(), which
 * resizes at random and asserts nothing. Here the invariants are checked.
 */

#include "anthy_test_util.h"

/* わたしはがくせいです — five morphemes, so the splitter has real work to do
 * and there is room to resize without immediately hitting a boundary. */
#define YOMI "わたしはがくせいです"
#define YOMI_CHARS 10   /* characters, not bytes */

static anthy_context_t
new_context(void)
{
  anthy_context_t ac = anthy_create_context();
  if (!ac) {
    AT_FAILF("anthy_create_context() returned NULL");
    exit(1);
  }
  anthy_context_set_encoding(ac, ANTHY_UTF8_ENCODING);
  return ac;
}

/* Sum of every segment's length must always equal the input length: that is
 * the one thing resizing may never break, and it is cheap to check. */
static int
total_length(anthy_context_t ac, const char *what)
{
  struct anthy_conv_stat cs;
  int i, total = 0;

  if (anthy_get_stat(ac, &cs) != 0 || cs.nr_segment < 1) {
    AT_FAILF("%s: no segments", what);
    return -1;
  }
  for (i = 0; i < cs.nr_segment; i++) {
    struct anthy_segment_stat ss;
    if (anthy_get_segment_stat(ac, i, &ss) != 0) {
      AT_FAILF("%s: segment %d has no stat", what, i);
      return -1;
    }
    if (ss.seg_len < 1)
      AT_FAILF("%s: segment %d has length %d", what, i, ss.seg_len);
    if (ss.nr_candidate < 1)
      AT_FAILF("%s: segment %d has no candidates", what, i);
    total += ss.seg_len;
  }
  return total;
}

static void
test_resize(void)
{
  anthy_context_t ac = new_context();
  struct anthy_conv_stat before, after;

  anthy_set_string(ac, YOMI);
  AT_CHECK_INT(total_length(ac, "initial"), YOMI_CHARS);
  anthy_get_stat(ac, &before);
  if (before.nr_segment < 2) {
    AT_FAILF("\"%s\" split into %d segment(s); expected at least 2",
             YOMI, before.nr_segment);
    anthy_release_context(ac);
    return;
  }

  /* Grow the first segment, then shrink it back. Length is preserved
   * throughout — anthy compensates in the following segments. */
  anthy_resize_segment(ac, 0, 1);
  AT_CHECK_INT(total_length(ac, "after +1"), YOMI_CHARS);
  anthy_resize_segment(ac, 0, -1);
  AT_CHECK_INT(total_length(ac, "after -1"), YOMI_CHARS);

  /* Collapsing everything into one segment must still be self-consistent.
   * Nine +1s is more than enough for a ten-character reading; the extra ones
   * are no-ops at the boundary, which is itself worth not crashing on. */
  {
    int i;
    for (i = 0; i < YOMI_CHARS; i++)
      anthy_resize_segment(ac, 0, 1);
  }
  AT_CHECK_INT(total_length(ac, "after collapse"), YOMI_CHARS);
  anthy_get_stat(ac, &after);
  AT_CHECK_INT(after.nr_segment, 1);

  /* Resetting must return the context to a usable, empty state rather than
   * leaving the freed segment list behind. */
  anthy_reset_context(ac);
  anthy_get_stat(ac, &after);
  AT_CHECK_INT(after.nr_segment, 0);
  anthy_set_string(ac, YOMI);
  AT_CHECK_INT(total_length(ac, "after reset"), YOMI_CHARS);

  anthy_release_context(ac);
}

static void
test_commit(void)
{
  anthy_context_t ac = new_context();
  struct anthy_conv_stat cs;
  char buf[256];
  int i;

  anthy_set_string(ac, YOMI);
  anthy_get_stat(ac, &cs);

  /* Committing every segment in turn is what an IME does when the user
   * presses Enter; it writes the choices into the record store under HOME,
   * which is the other file anthy keeps there besides the private dictionary.
   * Nothing here should fail, and nothing should be left unreadable. */
  for (i = 0; i < cs.nr_segment; i++) {
    if (anthy_get_segment(ac, i, 0, buf, sizeof buf) <= 0)
      AT_FAILF("segment %d yielded no candidate before commit", i);
    if (anthy_commit_segment(ac, i, 0) != 0)
      AT_FAILF("anthy_commit_segment(%d, 0) failed", i);
  }
  anthy_release_context(ac);

  /* Second pass: the committed choices are now in the record, and the same
   * input must still convert to something self-consistent. This is where a
   * record file written in the wrong mode would show up as a corrupt read. */
  ac = new_context();
  anthy_set_string(ac, YOMI);
  AT_CHECK_INT(total_length(ac, "after commit history"), YOMI_CHARS);
  anthy_release_context(ac);
}

/*
 * Prediction.
 *
 * Upstream ships test/prediction.c for this, but its main() begins
 * `if (argc == 1) return 0;` and meson runs it without an argument, so the
 * vendored test never reaches a prediction call.
 *
 * anthy predicts purely from the PREDICTION section of the record store —
 * src-main/context.c's anthy_do_set_prediction_str() consults nothing else —
 * and that section is only written by anthy_proc_commit(), which fires when
 * the last segment of a context is committed. So a prediction test that does
 * not first commit something is testing an empty database, and passes on a
 * platform where the record cannot be written at all. Teach it a reading here,
 * then ask for it back by prefix.
 */
#define PREDICT_YOMI   "にほんごのへんかん"
#define PREDICT_PREFIX "にほんご"

static void
test_prediction(void)
{
  anthy_context_t ac;
  struct anthy_conv_stat cs;
  struct anthy_prediction_stat ps;
  char learned[256] = { 0 };
  int i, matched = 0;

  /* Teach: commit every segment, which is what triggers the learning. */
  ac = new_context();
  anthy_set_string(ac, PREDICT_YOMI);
  anthy_get_stat(ac, &cs);
  if (anthy_get_segment(ac, 0, 0, learned, sizeof learned) <= 0)
    AT_FAILF("no candidate to learn for \"%s\"", PREDICT_YOMI);
  for (i = 0; i < cs.nr_segment; i++) {
    if (anthy_commit_segment(ac, i, 0) != 0)
      AT_FAILF("anthy_commit_segment(%d, 0) failed while learning", i);
  }
  anthy_release_context(ac);

  /* Recall, from a fresh context, by a prefix of the reading. */
  ac = new_context();
  AT_CHECK_INT(anthy_set_prediction_string(ac, PREDICT_PREFIX), 0);
  AT_CHECK_INT(anthy_get_prediction_stat(ac, &ps), 0);
  if (ps.nr_prediction < 1) {
    AT_FAILF("no predictions for \"%s\" after committing \"%s\" — the "
             "record store under HOME is not being written",
             PREDICT_PREFIX, PREDICT_YOMI);
    anthy_release_context(ac);
    return;
  }

  for (i = 0; i < ps.nr_prediction; i++) {
    char buf[256];
    int need = anthy_get_prediction(ac, i, NULL, 0);
    if (need <= 0 || need >= (int)sizeof buf) {
      AT_FAILF("prediction %d: bad length %d", i, need);
      continue;
    }
    if (anthy_get_prediction(ac, i, buf, sizeof buf) != need) {
      AT_FAILF("prediction %d: length disagreed between the two calls", i);
      continue;
    }
    if ((int)strlen(buf) != need) {
      AT_FAILF("prediction %d: returned %d but wrote %d bytes",
               i, need, (int)strlen(buf));
      continue;
    }
    if (strcmp(buf, learned) == 0)
      matched++;
  }
  if (!matched)
    AT_FAILF("none of the %d prediction(s) for \"%s\" was the committed "
             "\"%s\"", ps.nr_prediction, PREDICT_PREFIX, learned);

  AT_CHECK_INT(anthy_commit_prediction(ac, 0), 0);
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

  test_resize();
  test_commit();
  test_prediction();

  anthy_quit();
  return at_report("test_segment");
}
