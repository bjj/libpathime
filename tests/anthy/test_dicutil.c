/*
 * Private-dictionary round trip through <anthy/dicutil.h>.
 *
 * This is the one part of anthy that *writes* structured data at runtime. The
 * personal dictionary is a line-oriented text file under $HOME plus a trie
 * alongside it, and src-worddic/textdict.c navigates it by byte offset: it
 * fseek()s to an offset, fgets() a line, and advances the offset by
 * strlen(line). That accounting is only correct when the bytes the stream
 * yields are the bytes on disk — which on Windows is exactly what the CRT's
 * default text mode breaks, in both directions (a written '\n' becomes two
 * bytes, and a read "\r\n" becomes one). See BUILD.md, "Known Windows
 * limitations": the binary-mode fix was scoped to the codegen executables,
 * because a library must not change _fmode under its host process.
 *
 * So this test is deliberately pointed straight at that gap. Each phase tears
 * the library down and brings it back up, so every read has to come off disk —
 * a same-process read could be served from memory and would prove nothing.
 * The delete phase matters as much as the write: anthy_textdict_delete_line()
 * is the code that mixes an mmap'd (raw) view of the file with fgets()
 * (translated) lengths, so it is where a text-mode discrepancy does damage.
 *
 * HOME is redirected into the build tree (ANTHY_TEST_HOME), so nothing here
 * touches the invoking user's real dictionary.
 */

#include "anthy_test_util.h"
#include <anthy/dicutil.h>

#define WT "#T35"   /* noun; the part-of-speech every hand-added word gets */

struct entry {
  const char *yomi;
  const char *word;
  int freq;
};

/* Several entries, not one: the byte-offset walk over the dictionary file only
 * goes wrong from the second line onwards, so a single-entry test would pass
 * on a platform where iteration is broken. */
static const struct entry entries[] = {
  { "ぱすたいむ",     "パス多夢",   500 },
  { "りぶぱすたいむ", "libpathime", 400 },
  { "てすとたんご",   "試験単語",   300 },
};
static const int nr_entries = (int)(sizeof entries / sizeof entries[0]);

/*
 * Note what is *not* here: anthy_dic_util_set_personality(). Calling it after
 * anthy_dic_util_init() corrupts the heap and aborts the process — verified on
 * both Windows (STATUS_HEAP_CORRUPTION) and Linux/glibc ("corrupted size vs.
 * prev_size"), so this is an upstream bug rather than anything to do with the
 * port. anthy_dic_set_personality() (src-worddic/word_dic.c) overwrites
 * anthy_current_record and anthy_current_personal_dic_cache without releasing
 * the previous pair, and anthy_dic_util_init() has already installed one under
 * the id "default"; the orphaned record is then destroyed a second time by
 * anthy_quit_allocator() during teardown. Nothing in anthy's own tree calls
 * the function — it is a public API with no in-tree caller — which is
 * presumably why it has survived.
 *
 * The default personality is all this test needs anyway: TEST_HOME is already
 * private to it, so there is nothing to collide with.
 */
static void
dicutil_start(void)
{
  at_point_at_build_tree();
  anthy_dic_util_init();
  anthy_dic_util_set_encoding(ANTHY_UTF8_ENCODING);
}

static void
write_entries(void)
{
  int i;
  for (i = 0; i < nr_entries; i++) {
    int r = anthy_priv_dic_add_entry(entries[i].yomi, entries[i].word,
                                     WT, entries[i].freq);
    if (r != ANTHY_DIC_UTIL_OK)
      AT_FAILF("anthy_priv_dic_add_entry(\"%s\") returned %d",
               entries[i].yomi, r);
  }
}

/* Walk the whole dictionary and tick off the entries we put in it. */
static void
read_back_entries(void)
{
  int seen[sizeof entries / sizeof entries[0]] = { 0 };
  int visited = 0, i;

  if (anthy_priv_dic_select_first_entry() != 0) {
    AT_FAILF("anthy_priv_dic_select_first_entry() found nothing — %d entries "
             "were written", nr_entries);
    return;
  }

  do {
    char yomi[256] = { 0 }, word[256] = { 0 }, wt[64] = { 0 };
    int freq;

    if (!anthy_priv_dic_get_index(yomi, (int)sizeof yomi)) {
      AT_FAILF("entry %d: anthy_priv_dic_get_index() failed", visited);
      break;
    }
    if (!anthy_priv_dic_get_word(word, (int)sizeof word))
      AT_FAILF("entry %d (\"%s\"): anthy_priv_dic_get_word() failed",
               visited, yomi);
    if (!anthy_priv_dic_get_wtype(wt, (int)sizeof wt))
      AT_FAILF("entry %d (\"%s\"): anthy_priv_dic_get_wtype() failed",
               visited, yomi);
    freq = anthy_priv_dic_get_freq();

    for (i = 0; i < nr_entries; i++) {
      if (strcmp(yomi, entries[i].yomi) != 0)
        continue;
      seen[i]++;
      AT_CHECK_STR(word, entries[i].word);
      AT_CHECK_INT(freq, entries[i].freq);
      /* anthy stores the wtype string verbatim; anything else means the line
       * was split at the wrong byte. */
      AT_CHECK_STR(wt, WT);
      break;
    }
    if (i == nr_entries)
      AT_FAILF("unexpected entry \"%s\" -> \"%s\" in the private dictionary",
               yomi, word);

    if (++visited > 100) {
      AT_FAILF("iteration did not terminate after 100 entries");
      break;
    }
  } while (anthy_priv_dic_select_next_entry() == 0);

  for (i = 0; i < nr_entries; i++) {
    if (seen[i] != 1)
      AT_FAILF("entry \"%s\" -> \"%s\" was read back %d times, expected once",
               entries[i].yomi, entries[i].word, seen[i]);
  }
}

int
main(void)
{
  at_check_source_encoding();

  /* Phase 1: write, then shut the library down so everything is flushed. */
  dicutil_start();
  anthy_priv_dic_delete();     /* start from a known-empty dictionary */
  write_entries();
  anthy_dic_util_quit();

  /* Phase 2: a fresh init, so the entries have to come off disk. */
  dicutil_start();
  read_back_entries();
  anthy_dic_util_quit();

  /* Phase 3: delete everything. This is the offset-arithmetic path. */
  dicutil_start();
  anthy_priv_dic_delete();
  anthy_dic_util_quit();

  /* Phase 4: and it really has to be empty afterwards — a partial delete
   * leaves entries that the next run would then see. It also leaves TEST_HOME
   * clean so the test is repeatable without a build directory wipe. */
  dicutil_start();
  if (anthy_priv_dic_select_first_entry() == 0) {
    char yomi[256] = { 0 };
    anthy_priv_dic_get_index(yomi, (int)sizeof yomi);
    AT_FAILF("anthy_priv_dic_delete() left \"%s\" behind", yomi);
  }
  anthy_dic_util_quit();

  return at_report("test_dicutil");
}
