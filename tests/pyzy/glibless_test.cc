/* GlibLess, tested directly.
 *
 * GlibLess.h/.cc is the one body of code the pyzy `libpathime` branch adds
 * rather than repairs: the glib calls the library makes, implemented without
 * glib. The conversion tests exercise most of it incidentally, but only ever
 * on the happy path of one machine's locale and one run's filesystem state.
 * This suite pins the contracts the call sites actually depend on, the two
 * of them that are easy to lose foremost:
 *
 *   - g_rename must replace an existing target. Database.cc renames the
 *     freshly written user database over the previous one on every save
 *     after the first; a rename with POSIX-only semantics on Windows would
 *     silently drop user learning at shutdown, and a single ctest run never
 *     notices because the first save has no existing target.
 *   - the filesystem calls take UTF-8 and must survive a path component the
 *     active Windows code page cannot express.
 *
 * Everything here must build and pass on Linux as well, like the rest of
 * this directory: the point is that both platforms implement the same
 * contract, not that Windows merely compiles.
 */
#include <fstream>
#include <string>

#include "pyzy_private.h"
#include "pyzy_assert.h"

namespace {

const std::string home = PYZY_TEST_HOME;

std::string join (const std::string &base, const char *leaf)
{
    return base + G_DIR_SEPARATOR_S + leaf;
}

std::string read_file (const std::string &path)
{
    std::ifstream in (path.c_str ());
    std::string line;
    std::getline (in, line);
    return line;
}

bool callback_fired = false;

gboolean timeout_callback (void *data)
{
    (void) data;
    callback_fired = true;
    return 0;
}

void test_strings (void)
{
    /* g_strlcpy: BSD contract — always NUL-terminate, return the length it
     * tried to copy. Query::fill() truncates candidate phrases with it. */
    char buf[8];
    PYZY_EXPECT_INT (g_strlcpy (buf, "0123456789", sizeof (buf)), 10);
    PYZY_EXPECT_STR (std::string (buf), "0123456");
    PYZY_EXPECT_INT (g_strlcpy (buf, "ab", sizeof (buf)), 2);
    PYZY_EXPECT_STR (std::string (buf), "ab");

    /* g_strlcat: Phrase::operator+= concatenates phrase text with it. */
    PYZY_EXPECT_INT (g_strlcat (buf, "cdefgh", sizeof (buf)), 8);
    PYZY_EXPECT_STR (std::string (buf), "abcdefg");

    /* g_strdup_vprintf reaches the tests through String::printf, which is
     * how every SQL statement in Database.cc is assembled. */
    PyZy::String s;
    s.printf ("%s=%d", "n", 42);
    PYZY_EXPECT_STR (std::string (s), "n=42");
    s.appendPrintf (";%c", '!');
    PYZY_EXPECT_STR (std::string (s), "n=42;!");

    char small[16];
    g_snprintf (small, sizeof (small), "%d-%d", 7, 9);
    PYZY_EXPECT_STR (std::string (small), "7-9");
}

void test_utf8 (void)
{
    /* Valid text, including a supplementary-plane scalar. */
    PYZY_EXPECT (g_utf8_validate ("a\xE4\xB8\x96\xF0\x9F\x98\x80z", -1, NULL));
    /* Overlong form, surrogate, past U+10FFFF, truncated sequence. */
    PYZY_EXPECT (!g_utf8_validate ("\xC0\xAF", -1, NULL));
    PYZY_EXPECT (!g_utf8_validate ("\xED\xA0\x80", -1, NULL));
    PYZY_EXPECT (!g_utf8_validate ("\xF4\x90\x80\x80", -1, NULL));
    PYZY_EXPECT (!g_utf8_validate ("a\xE4\xB8", -1, NULL));

    const char *text = "a\xE4\xB8\x96z";   /* a 世 z */
    PYZY_EXPECT_INT (g_utf8_strlen (text, -1), 3);
    PYZY_EXPECT (g_utf8_offset_to_pointer (text, 2) == text + 4);
    PYZY_EXPECT (g_utf8_prev_char (text + 4) == text + 1);

    char enc[8];
    PYZY_EXPECT_INT (g_unichar_to_utf8 ('A', enc), 1);
    PYZY_EXPECT_INT (g_unichar_to_utf8 (0x4E16, enc), 3);
    PYZY_EXPECT_STR (std::string (enc, 3), "\xE4\xB8\x96");
    PYZY_EXPECT_INT (g_unichar_to_utf8 (0x1F600, enc), 4);
    PYZY_EXPECT_INT (g_unichar_to_utf8 (0x110000, enc), 0);

    /* The String.h caller passes -1 (NUL-terminated) and reads only
     * error->message on failure. */
    const PyZy::unichar good[] = {0x4E16, 0x754C, 0};
    GError *error = NULL;
    char *converted = g_ucs4_to_utf8 (good, -1, NULL, NULL, &error);
    PYZY_EXPECT (converted != NULL);
    PYZY_EXPECT_STR (std::string (converted), "\xE4\xB8\x96\xE7\x95\x8C");
    g_free (converted);

    const PyZy::unichar bad[] = {0x110000, 0};
    converted = g_ucs4_to_utf8 (bad, -1, NULL, NULL, &error);
    PYZY_EXPECT (converted == NULL);
    PYZY_EXPECT (error != NULL && error->message != NULL);
    g_error_free (error);
}

void test_paths (void)
{
    char *built = g_build_filename ("a", "b", NULL);
    PYZY_EXPECT_STR (std::string (built), join ("a", "b"));
    g_free (built);

    /* A base that already ends in a separator gets no second one — the shape
     * DataDir hands to SpecialPhraseTable. */
    built = g_build_filename ("a" G_DIR_SEPARATOR_S, "b", NULL);
    PYZY_EXPECT_STR (std::string (built), "a" G_DIR_SEPARATOR_S "b");
    g_free (built);

    PYZY_EXPECT (g_get_user_cache_dir () != NULL);
    PYZY_EXPECT (g_get_user_cache_dir ()[0] != '\0');
    PYZY_EXPECT (g_get_user_config_dir () != NULL);
    PYZY_EXPECT (g_get_user_config_dir ()[0] != '\0');
}

void test_filesystem (void)
{
    /* A path component the active Windows code page cannot express, so a
     * shim that fell back to the narrow CRT would fail here. Nested, so
     * mkdir_with_parents proves the walk and not just one mkdir. */
    const std::string deep =
        join (join (home, "glibless-\xE7\x9B\xAE\xE5\xBD\x95"), "nested");
    PYZY_EXPECT_INT (g_mkdir_with_parents (deep.c_str (), 0700), 0);
    /* Only G_FILE_TEST_IS_REGULAR exists; a directory is not a regular file. */
    PYZY_EXPECT (!g_file_test (deep.c_str (), G_FILE_TEST_IS_REGULAR));

    /* Leftovers from an earlier run are legitimate state, not an error:
     * the replace-rename below must cope with them anyway. */
    const std::string target = join (deep, "renamed.txt");
    const std::string plain = join (home, "plain.txt");
    const std::string back = join (home, "back.txt");
    g_unlink (back.c_str ());

    {
        std::ofstream out (plain.c_str ());
        out << "one\n";
    }
    PYZY_EXPECT (g_file_test (plain.c_str (), G_FILE_TEST_IS_REGULAR));
    PYZY_EXPECT_INT (g_rename (plain.c_str (), target.c_str ()), 0);
    PYZY_EXPECT (!g_file_test (plain.c_str (), G_FILE_TEST_IS_REGULAR));
    PYZY_EXPECT (g_file_test (target.c_str (), G_FILE_TEST_IS_REGULAR));

    /* The contract Database.cc's saveUserDB() depends on: renaming over an
     * existing target replaces it. */
    {
        std::ofstream out (plain.c_str ());
        out << "two\n";
    }
    PYZY_EXPECT_INT (g_rename (plain.c_str (), target.c_str ()), 0);

    /* Read the survivor back through an ASCII name, so the check does not
     * itself depend on the C++ streams opening a non-ASCII path. */
    PYZY_EXPECT_INT (g_rename (target.c_str (), back.c_str ()), 0);
    PYZY_EXPECT_STR (read_file (back), "two");

    PYZY_EXPECT_INT (g_unlink (back.c_str ()), 0);
    PYZY_EXPECT (!g_file_test (back.c_str (), G_FILE_TEST_IS_REGULAR));
}

void test_timer_and_timeout (void)
{
    GTimer *timer = g_timer_new ();
    unsigned long usec = 0;
    const double elapsed = g_timer_elapsed (timer, &usec);
    PYZY_EXPECT (elapsed >= 0.0 && elapsed < 3600.0);
    PYZY_EXPECT (usec < 1000000);
    g_timer_start (timer);
    PYZY_EXPECT (g_timer_elapsed (timer, NULL) >= 0.0);
    g_timer_destroy (timer);

    /* The id is a sentinel: nonzero, so ~Database saves the user database,
     * and the callback never fires because nothing runs a main loop. */
    const unsigned int id =
        g_timeout_add_seconds (60, timeout_callback, NULL);
    PYZY_EXPECT (id != 0);
    PYZY_EXPECT (g_source_remove (id));
    PYZY_EXPECT (!callback_fired);
}

void test_macros (void)
{
    static const int table[] = {3, 1, 4, 1, 5};
    PYZY_EXPECT_INT (G_N_ELEMENTS (table), 5);
    PYZY_EXPECT_INT (MIN (2, 7), 2);
    PYZY_EXPECT_INT (GPOINTER_TO_INT (GINT_TO_POINTER (0x4E16)), 0x4E16);
    PYZY_EXPECT (G_LIKELY (true));
    PYZY_EXPECT (!G_UNLIKELY (false));
}

}  // namespace

int main (void)
{
    test_strings ();
    test_utf8 ();
    test_paths ();
    test_filesystem ();
    test_timer_and_timeout ();
    test_macros ();
    return PYZY_TEST_RESULT;
}
