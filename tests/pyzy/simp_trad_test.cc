/* PyZy::SimpTradConverter — simplified to traditional Chinese.
 *
 * pyzy can use OpenCC for this, but the port deliberately leaves HAVE_OPENCC
 * undefined, so what runs is the in-tree implementation over the committed
 * 7403-entry SimpTradConverterTable.h. That code path is pure table lookup with
 * no database and no configuration, which makes it a clean probe of two things
 * the Windows build has to get right: that a large generated table of UTF-8
 * string literals survives compilation intact (MSVC needs /utf-8 or it decodes
 * the source as the host ANSI codepage), and that String's append operators
 * behave the same as on Linux.
 */
#include "pyzy_assert.h"
#include "pyzy_private.h"   /* generated: String, SimpTradConverter */

#include <string>

using PyZy::SimpTradConverter;
using PyZy::String;

namespace {

std::string convert (const char *in)
{
    String out;
    SimpTradConverter::simpToTrad (in, out);
    return std::string (out.c_str (), out.size ());
}

void checkPassThrough ()
{
    /* Nothing to convert must come back byte-identical, including input the
     * table has no entry for at all. */
    PYZY_EXPECT_STR (convert (""), "");
    PYZY_EXPECT_STR (convert ("abc 123"), "abc 123");
    PYZY_EXPECT_STR (convert ("中"), "中");
}

void checkSingleCharacters ()
{
    PYZY_EXPECT_STR (convert ("头"), "頭");
    PYZY_EXPECT_STR (convert ("发"), "發");
    PYZY_EXPECT_STR (convert ("条"), "條");
    PYZY_EXPECT_STR (convert ("龟"), "龜");
}

/* The converter matches the longest run it can, up to SIMP_TO_TRAD_MAX_LEN
 * characters, before falling back to shorter ones. These cases can only come
 * out right if that search works: character-by-character, "头发" would convert
 * to 頭發 rather than 頭髮, and "什么" would not convert at all because neither
 * character has an entry of its own. */
void checkLongestMatchWins ()
{
    PYZY_EXPECT_STR (convert ("头发"), "頭髮");
    PYZY_EXPECT_STR (convert ("什么"), "什麼");
    PYZY_EXPECT_STR (convert ("面条"), "麵條");
}

/* Mixed input drives the "no entry, emit the character and advance" branch
 * between matches, and mixes multi-byte with single-byte characters. */
void checkMixedText ()
{
    PYZY_EXPECT_STR (convert ("中国"), "中國");
    PYZY_EXPECT_STR (convert ("a头发b"), "a頭髮b");
    PYZY_EXPECT_STR (convert ("头发和面条"), "頭髮和麵條");
}

}  // namespace

int main ()
{
    checkPassThrough ();
    checkSingleCharacters ();
    checkLongestMatchWins ();
    checkMixedText ();
    return PYZY_TEST_RESULT;
}
