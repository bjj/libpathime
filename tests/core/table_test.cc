/*
 * The table engine's data layer, below the seam: the source parser, the
 * declaration, the LIKE-pattern construction, and candidate ordering.
 *
 * These are reachable from tests/api only through their effects on real tables,
 * where a wrong answer shows up as a differently ordered candidate list and a
 * right answer proves little about the edge cases. Here they are called
 * directly, against inputs chosen to be awkward: the synthetic table from
 * the mapping doc's minimal example, wildcard characters that collide with SQL's own,
 * and rule sets that are malformed in each of the ways §3.5 forbids.
 *
 * Nothing here opens a database. That is the point of the split the directory
 * is organized around — the parser and the ranking know nothing about SQLite,
 * about compositions, or about options — and it is why this test compiles four
 * source files rather than the whole library.
 */

#include <sstream>
#include <string>
#include <vector>

#include "engines/table/coverage.h"
#include "engines/table/ranking.h"
#include "engines/table/table_db.h"
#include "engines/table/table_properties.h"
#include "engines/table/table_source.h"
#include "engines/table/variants.h"

#include "core_test_util.h"

using namespace pathime;
using namespace pathime::table;

namespace {

/* A minimal table: no goucima, no rules, three single-key entries and a pair. */
const char *const kSyntheticTable =
    "BEGIN_DEFINITION\n"
    "NAME = Example\n"
    "UUID = 00000000-0000-0000-0000-000000000000\n"
    "SERIAL_NUMBER = 20240101\n"
    "LANGUAGES = en\n"
    "VALID_INPUT_CHARS = abc\n"
    "MAX_KEY_LENGTH = 2\n"
    "AUTO_COMMIT = FALSE\n"
    "END_DEFINITION\n"
    "\n"
    "BEGIN_TABLE\n"
    "a\t\xCE\xB1\t1000\n"
    "b\t\xCE\xB2\t1000\n"
    "c\t\xCE\xB3\t1000\n"
    "ab\t\xCE\xB1\xCE\xB2\t500\n"
    "END_TABLE\n";

TableSource parse(const std::string &text)
{
    std::istringstream in(text);
    TableSource source;
    std::string error;
    PT_CHECK(parse_table_source(in, &source, &error));
    return source;
}

void test_synthetic_table()
{
    const TableSource source = parse(kSyntheticTable);

    PT_CHECK(source.properties.name == "Example");
    PT_CHECK(source.properties.serial_number == 20240101u);
    PT_CHECK(source.properties.max_key_length == 2);
    PT_CHECK(!source.properties.auto_commit);
    PT_CHECK(source.phrases.size() == 4);
    PT_CHECK(source.skipped_rows == 0);

    /* `en` is neither Chinese nor CJK, so neither variant filtering nor
     * full-width conversion applies to this table. */
    PT_CHECK(!source.properties.is_chinese);
    PT_CHECK(!source.properties.is_cjk);

    /* VALID_INPUT_CHARS is a set of scalars, not a string to search. */
    PT_CHECK(source.properties.is_input_char(U'a'));
    PT_CHECK(source.properties.is_input_char(U'c'));
    PT_CHECK(!source.properties.is_input_char(U'd'));

    /* With no START_CHARS, anything that may extend a run may begin one. */
    PT_CHECK(source.properties.is_start_char(U'a'));
    PT_CHECK(!source.properties.is_start_char(U'd'));
}

void test_comments_and_scim_header()
{
    const TableSource source = parse(
        "SCIM_Generic_Table_Phrase_Library_TEXT\n"
        "VERSION_1_0\n"
        "### a comment line\n"
        "BEGIN_DEFINITION\n"
        "NAME = Commented   ### trailing comment\n"
        "MAX_KEY_LENGTH = 3\n"
        "END_DEFINITION\n"
        "BEGIN_TABLE\n"
        "a\tX\t1\n"
        "END_TABLE\n");

    PT_CHECK(source.properties.name == "Commented");
    PT_CHECK(source.properties.max_key_length == 3);
    PT_CHECK(source.phrases.size() == 1);
}

void test_nosymbol_and_extra_column()
{
    const TableSource source = parse(
        "BEGIN_DEFINITION\nNAME = N\nEND_DEFINITION\n"
        "BEGIN_TABLE\n"
        "a\tNOSYMBOL\t1\n"
        "b\tX\t2\tignored-fourth-column\n"
        "END_TABLE\n"
        "BEGIN_TABLE_EXTRA\n"
        "c\tY\t3\n"
        "END_TABLE_EXTRA\n");

    PT_CHECK(source.phrases.size() == 3);
    PT_CHECK(source.phrases[0].phrase.empty());  /* NOSYMBOL is the empty string */
    PT_CHECK(source.phrases[1].phrase == "X");
    PT_CHECK(source.phrases[1].freq == 2);
    /* TABLE_EXTRA merges into the same list (§3.2). */
    PT_CHECK(source.phrases[2].phrase == "Y");
}

void test_malformed_rows_are_skipped()
{
    /*
     * The two shapes that actually occur in ibus-table-chinese: a stray marker
     * line inside the table section (stroke5.txt) and a row whose first
     * separator is a space rather than a tab (cantonese.txt). Both are skipped
     * and counted, because ibus-table loads those tables and a stricter parser
     * would refuse data that works.
     */
    const TableSource source = parse(
        "BEGIN_DEFINITION\nNAME = N\nEND_DEFINITION\n"
        "BEGIN_TABLE\n"
        "a\tX\t1\n"
        "%chardef end\n"
        "huk \xE9\xB5\xA0\t0\n"
        "b\tY\t2\n"
        "c\tZ\tnot-a-number\n"
        "END_TABLE\n");

    PT_CHECK(source.phrases.size() == 2);
    PT_CHECK(source.skipped_rows == 3);
}

void test_char_prompts()
{
    const TableSource source = parse(
        "BEGIN_DEFINITION\n"
        "NAME = Prompted\n"
        "BEGIN_CHAR_PROMPTS_DEFINITION\n"
        "a \xE6\x97\xA5\n"
        "b \xE6\x9C\x88\n"
        "END_CHAR_PROMPTS_DEFINITION\n"
        "MAX_KEY_LENGTH = 5\n"
        "END_DEFINITION\n"
        "BEGIN_TABLE\na\tX\t1\nEND_TABLE\n");

    /* The nested section closes back into DEFINITION, so keys after it parse. */
    PT_CHECK(source.properties.max_key_length == 5);
    PT_CHECK(source.properties.char_prompts.size() == 2);
    PT_CHECK(source.properties.char_prompts.at(U'a') == "\xE6\x97\xA5");
    PT_CHECK(source.properties.char_prompts.at(U'b') == "\xE6\x9C\x88");
}

void test_goucima_derivation()
{
    TableSource source = parse(
        "BEGIN_DEFINITION\nNAME = N\nUSER_CAN_DEFINE_PHRASE = TRUE\nEND_DEFINITION\n"
        "BEGIN_TABLE\n"
        "a\tX\t1\n"
        "abc\tX\t1\n"
        "ab\tX\t1\n"
        "de\tXY\t1\n"
        "END_TABLE\n");

    derive_goucima(&source);

    /* "the longest tabkeys sequence that produces exactly that character" */
    PT_CHECK(source.goucima.at("X") == "abc");
    /* A multi-character phrase contributes no goucima of its own. */
    PT_CHECK(source.goucima.find("XY") == source.goucima.end());
}

void test_rules()
{
    Rules rules;
    PT_CHECK(parse_rules("ce2:p11+p12+p21+p22;ce3:p11+p21+p31+p32;ca4:p11+p21+p31+p-11",
                         &rules));
    PT_CHECK(rules.above == 4);
    PT_CHECK(rules.exact.size() == 3);
    PT_CHECK(rules.exact.at(2).size() == 4);
    PT_CHECK(rules.exact.at(2)[0] == Rules::Position(1, 1));
    PT_CHECK(rules.exact.at(4)[3] == Rules::Position(-1, 1));  /* p-11 counts from the end */

    /* The catch-all covers every length above its threshold. */
    PT_CHECK(rules.for_length(2) == &rules.exact.at(2));
    PT_CHECK(rules.for_length(9) == &rules.exact.at(4));
    PT_CHECK(rules.for_length(1) == nullptr);

    /* Each way §3.5 says a rule set may not be written. */
    Rules rejected;
    PT_CHECK(!parse_rules("ce2", &rejected));                       /* no colon */
    PT_CHECK(!parse_rules("ce1:p11", &rejected));                   /* below two characters */
    PT_CHECK(!parse_rules("ce2:p11;ca3:p11;ca4:p11", &rejected));   /* two catch-alls */
    PT_CHECK(!parse_rules("ca2:p11;ce4:p11", &rejected));           /* ca is not the largest */
    PT_CHECK(!parse_rules("ce2:q11", &rejected));                   /* not a position token */
    PT_CHECK(!parse_rules("ce2:p10", &rejected));                   /* positions are 1-based */

    /* An empty RULES is not an error; it means the table has none. */
    Rules empty;
    PT_CHECK(parse_rules("", &empty));
    PT_CHECK(empty.empty());
}

void test_commit_boundaries()
{
    /*
     * From RULES: the output length of each ceN below the catch-all threshold,
     * plus MAX_KEY_LENGTH which a run can never pass.
     */
    TableProperties with_rules;
    with_rules.set("MAX_KEY_LENGTH", "5");
    with_rules.set("RULES", "ce2:p11+p12+p21+p22;ce3:p11+p21+p31;ca4:p11+p21+p31+p-11");
    with_rules.finalize();

    const std::set<size_t> boundaries = with_rules.commit_boundaries();
    PT_CHECK(boundaries.count(4) == 1);  /* ce2 emits four positions */
    PT_CHECK(boundaries.count(3) == 1);  /* ce3 emits three */
    PT_CHECK(boundaries.count(5) == 1);  /* MAX_KEY_LENGTH */

    /* Without rules, LEAST_COMMIT_LENGTH spans up to MAX_KEY_LENGTH. */
    TableProperties least;
    least.set("MAX_KEY_LENGTH", "4");
    least.set("LEAST_COMMIT_LENGTH", "2");
    least.finalize();

    const std::set<size_t> spans = least.commit_boundaries();
    PT_CHECK(spans.count(1) == 0);
    PT_CHECK(spans.count(2) == 1);
    PT_CHECK(spans.count(3) == 1);
    PT_CHECK(spans.count(4) == 1);

    /* With neither, the only boundary is the maximum itself. */
    TableProperties plain;
    plain.set("MAX_KEY_LENGTH", "4");
    plain.finalize();
    PT_CHECK(plain.commit_boundaries().size() == 1);
    PT_CHECK(plain.commit_boundaries().count(4) == 1);
}

void test_like_pattern()
{
    TableProperties plain;
    plain.finalize();

    /* AUTO_WILDCARD defaults on, so a partial run matches longer entries. */
    LikePattern pattern = build_like_pattern(plain, "ab");
    PT_CHECK(pattern.pattern == "ab%");
    PT_CHECK(pattern.escape == '!');

    /* SQL's own wildcards are escaped when the table has not claimed them. */
    TableProperties no_auto;
    no_auto.set("AUTO_WILDCARD", "FALSE");
    no_auto.finalize();
    PT_CHECK(build_like_pattern(no_auto, "a%b_c").pattern == "a!%b!_c");

    /* The escape character itself is doubled where it is typed literally. */
    PT_CHECK(build_like_pattern(no_auto, "a!b").pattern == "a!!b");

    /* Configured wildcards become SQL's, and are no longer escaped. */
    TableProperties wild;
    wild.set("AUTO_WILDCARD", "FALSE");
    wild.set("SINGLE_WILDCARD_CHAR", "?");
    wild.set("MULTI_WILDCARD_CHAR", "*");
    wild.finalize();
    PT_CHECK(build_like_pattern(wild, "a?b*c").pattern == "a_b%c");

    /*
     * A table claiming `!` as a wildcard forces the escape character to move,
     * or escaping and matching would be indistinguishable.
     */
    TableProperties collide;
    collide.set("AUTO_WILDCARD", "FALSE");
    collide.set("SINGLE_WILDCARD_CHAR", "!");
    collide.finalize();
    const LikePattern moved = build_like_pattern(collide, "a!%");
    PT_CHECK(moved.escape == '@');
    PT_CHECK(moved.pattern == "a_@%");
}

void test_variant_classification()
{
    /* 学 U+5B66 simplified, 學 U+5B78 traditional, 我 U+6211 both. */
    PT_CHECK(variant_mask(0x5B66) == kVariantSimplified);
    PT_CHECK(variant_mask(0x5B78) == kVariantTraditional);
    PT_CHECK(variant_mask(0x6211) == kVariantBoth);

    /* 龙 / 龍, the pair that motivated the range table's shape. */
    PT_CHECK(variant_mask(0x9F99) == kVariantSimplified);
    PT_CHECK(variant_mask(0x9F8D) == kVariantTraditional);

    /* Nothing outside Han is classified, so latin is never filtered out. */
    PT_CHECK(variant_mask('a') == kVariantBoth);

    /* A phrase is classified by its first character (§11.1). */
    PT_CHECK(phrase_variant_mask("\xE5\xAD\xA6\xE6\xA0\xA1") == kVariantSimplified);
    PT_CHECK(phrase_variant_mask("") == kVariantBoth);

    /* The two exclusive modes filter; the three mixed modes admit everything. */
    PT_CHECK(variant_admits(PATHIME_CHINESE_SIMPLIFIED_ONLY, kVariantSimplified));
    PT_CHECK(!variant_admits(PATHIME_CHINESE_SIMPLIFIED_ONLY, kVariantTraditional));
    PT_CHECK(variant_admits(PATHIME_CHINESE_ANY, kVariantTraditional));
    PT_CHECK(variant_admits(PATHIME_CHINESE_SIMPLIFIED_FIRST, kVariantTraditional));

    /* A mixed mode expresses its preference as a sort boost instead. */
    PT_CHECK(variant_boost(PATHIME_CHINESE_SIMPLIFIED_FIRST, kVariantSimplified) == 1);
    PT_CHECK(variant_boost(PATHIME_CHINESE_SIMPLIFIED_FIRST, kVariantTraditional) == 0);
    PT_CHECK(variant_boost(PATHIME_CHINESE_ANY, kVariantSimplified) == 0);
}

PhraseMatch match(const char *tabkeys, const char *phrase, int64_t freq, int64_t user_freq = 0)
{
    PhraseMatch out;
    out.tabkeys = tabkeys;
    out.phrase = phrase;
    out.freq = freq;
    out.user_freq = user_freq;
    return out;
}

void test_ranking()
{
    /* Key 1: an exact tabkeys match outranks everything, whatever its freq. */
    std::vector<PhraseMatch> matches{
        match("abc", "long", 9999),
        match("ab", "exact", 1),
    };
    rank_candidates("ab", PATHIME_CHINESE_ANY, false, &matches);
    PT_CHECK(matches.size() == 2);
    PT_CHECK(matches[0].phrase == "exact");

    /* Key 3: user_freq outranks freq, which is what makes learning visible. */
    matches = {match("ab", "common", 1000), match("ab", "learned", 1, 5)};
    rank_candidates("ab", PATHIME_CHINESE_ANY, false, &matches);
    PT_CHECK(matches[0].phrase == "learned");

    /* Key 5 then 6: freq descending, then shorter tabkeys first. */
    matches = {match("abcd", "low", 10), match("abc", "high", 100)};
    rank_candidates("zz", PATHIME_CHINESE_ANY, false, &matches);
    PT_CHECK(matches[0].phrase == "high");

    matches = {match("abcd", "longer", 50), match("abc", "shorter", 50)};
    rank_candidates("zz", PATHIME_CHINESE_ANY, false, &matches);
    PT_CHECK(matches[0].phrase == "shorter");

    /* Key 4: a mixed mode reorders rather than filters. 学 simplified, 學 not. */
    matches = {match("a", "\xE5\xAD\xB8", 100), match("a", "\xE5\xAD\xA6", 100)};
    rank_candidates("a", PATHIME_CHINESE_SIMPLIFIED_FIRST, true, &matches);
    PT_CHECK(matches.size() == 2);
    PT_CHECK(matches[0].phrase == "\xE5\xAD\xA6");

    /* An exclusive mode removes the other script outright. */
    matches = {match("a", "\xE5\xAD\xB8", 100), match("a", "\xE5\xAD\xA6", 100)};
    rank_candidates("a", PATHIME_CHINESE_TRADITIONAL_ONLY, true, &matches);
    PT_CHECK(matches.size() == 1);
    PT_CHECK(matches[0].phrase == "\xE5\xAD\xB8");

    /* Not a Chinese table: no filtering and no boost, whatever the mode. */
    matches = {match("a", "\xE5\xAD\xB8", 100), match("a", "\xE5\xAD\xA6", 100)};
    rank_candidates("a", PATHIME_CHINESE_TRADITIONAL_ONLY, false, &matches);
    PT_CHECK(matches.size() == 2);

    /* §8's cap, which is the engine's own and not PATHIME_OPT_MAX_CANDIDATES. */
    matches.clear();
    for (int i = 0; i < 150; ++i) {
        matches.push_back(match("aa", "x", i));
    }
    rank_candidates("a", PATHIME_CHINESE_ANY, false, &matches);
    PT_CHECK(matches.size() == kCandidateCap);
}

void test_frequency_transfer()
{
    TableSource target = parse(
        "BEGIN_DEFINITION\nNAME = T\nEND_DEFINITION\n"
        "BEGIN_TABLE\n"
        "a\tX\t1000\n"   /* exactly at the threshold: rewritten */
        "b\tY\t2000\n"   /* above it: rewritten */
        "c\tZ\t999\n"    /* below it: left alone */
        "d\tW\t1000\n"   /* at it, but absent from the frequency table */
        "END_TABLE\n");
    const TableSource frequencies = parse(
        "BEGIN_DEFINITION\nNAME = F\nEND_DEFINITION\n"
        "BEGIN_TABLE\n"
        "q\tX\t9000\n"
        "r\tY\t8000\n"
        "s\tZ\t7000\n"
        "END_TABLE\n");

    apply_frequency_transfer(&target, frequencies, 1000);

    /*
     * Usage frequency *plus* the threshold, which is what keeps every rewritten
     * row above every preserved one.
     */
    PT_CHECK(target.phrases[0].freq == 10000);
    PT_CHECK(target.phrases[1].freq == 9000);

    /* Below the threshold the table's own manual ordering stands, even though
     * this phrase does appear in the frequency table. */
    PT_CHECK(target.phrases[2].freq == 999);

    /* At the threshold but unknown to the frequency table: 0 + threshold, so it
     * keeps its place rather than being demoted below the manual entries. */
    PT_CHECK(target.phrases[3].freq == 1000);

    /*
     * The boundary is `>=`, not `>`. This is the case that made the real tables
     * silently unchanged: every primary entry in cangjie5 is exactly 1000, so a
     * strict comparison transfers nothing at all.
     */
    TableSource boundary = parse(
        "BEGIN_DEFINITION\nNAME = B\nEND_DEFINITION\n"
        "BEGIN_TABLE\na\tX\t1000\nEND_TABLE\n");
    apply_frequency_transfer(&boundary, frequencies, 1000);
    PT_CHECK(boundary.phrases[0].freq == 10000);

    /* Last occurrence wins in the frequency source, as the reference does. */
    TableSource repeated = parse(
        "BEGIN_DEFINITION\nNAME = R\nEND_DEFINITION\n"
        "BEGIN_TABLE\na\tX\t1000\nEND_TABLE\n");
    const TableSource duplicates = parse(
        "BEGIN_DEFINITION\nNAME = D\nEND_DEFINITION\n"
        "BEGIN_TABLE\nq\tX\t9000\nr\tX\t5\nEND_TABLE\n");
    apply_frequency_transfer(&repeated, duplicates, 1000);
    PT_CHECK(repeated.phrases[0].freq == 1005);
}

void test_declared_options()
{
    TableProperties properties;
    properties.set("AUTO_COMMIT", "TRUE");
    properties.set("LANGUAGE_FILTER", "cm3");
    properties.set("SINGLE_WILDCARD_CHAR", "?");
    properties.set("DYNAMIC_ADJUST", "TRUE");
    properties.finalize();

    int64_t value = 0;
    PT_CHECK(properties.declared_number(PATHIME_OPT_TABLE_AUTO_COMMIT, &value));
    PT_CHECK(value == 1);
    PT_CHECK(properties.declared_number(PATHIME_OPT_CHINESE_VARIANT, &value));
    PT_CHECK(value == PATHIME_CHINESE_TRADITIONAL_FIRST);

    /* A table that can learn declares learning on; one that cannot declares off. */
    PT_CHECK(properties.declared_number(PATHIME_OPT_LEARNING, &value));
    PT_CHECK(value == 1);

    TableProperties inert;
    inert.finalize();
    PT_CHECK(inert.declared_number(PATHIME_OPT_LEARNING, &value));
    PT_CHECK(value == 0);

    /* An undeclared LANGUAGE_FILTER contributes no tier-3 value at all. */
    PT_CHECK(!inert.declared_number(PATHIME_OPT_CHINESE_VARIANT, &value));

    /* Strings: the wildcards, and nothing else. */
    PT_CHECK(properties.declared_text(PATHIME_OPT_TABLE_SINGLE_WILDCARD) != nullptr);
    PT_CHECK(std::string(properties.declared_text(PATHIME_OPT_TABLE_SINGLE_WILDCARD)) == "?");
    PT_CHECK(properties.declared_text(PATHIME_OPT_TABLE_MULTI_WILDCARD) == nullptr);
    PT_CHECK(properties.declared_text(PATHIME_OPT_TABLE_FILE) == nullptr);
}

/*
 * The glyph-coverage map and the filter built on it.
 *
 * The specific code points asserted below are chosen to be structural rather
 * than incidental: ASCII and the common Han characters any CJK font has, against
 * a plane-2 code point no map this library ships reaches. They hold for either
 * map — LIBPATHIME_TABLE_COVERAGE selects which one is compiled in here, and it
 * is the one the shipped tables were trimmed with — and regenerating either from
 * newer fonts should not move any of them.
 *
 * What is deliberately *not* asserted is a range count or a total, which would
 * turn every font refresh into a test edit while telling nobody anything.
 */
void test_coverage()
{
    /* Covered: ASCII, common Han, and the CJK punctuation the tables emit. */
    PT_CHECK(is_covered(0x0041));  /* A */
    PT_CHECK(is_covered(0x6211));  /* 我 */
    PT_CHECK(is_covered(0x7684));  /* 的 */
    PT_CHECK(is_covered(0x3002));  /* 。 */

    /*
     * Not covered: a CJK Extension B code point outside the repertoire of either
     * map. This is the class of character the filter exists for — a table
     * carries it, the target cannot draw it, and it would otherwise sit in a
     * candidate list as tofu. It is also the class an embedder whose target
     * *does* have an Extension B font gives up by filtering at all, which is why
     * LIBPATHIME_TABLE_COVERAGE=none exists.
     */
    PT_CHECK(!is_covered(0x2A6D6));

    /* Every character counts, not just the first: one bad one condemns a phrase. */
    PT_CHECK(phrase_is_covered("我的"));
    PT_CHECK(!phrase_is_covered("我\xF0\xAA\x9B\x96"));  /* 我 + U+2A6D6 */

    /* An empty phrase has nothing that could fail to render. */
    PT_CHECK(phrase_is_covered(""));

    /*
     * The filter drops rows and reports how many, leaving the survivors in their
     * original order — the compiled row order is contract (§4.2), so a filter
     * that reordered would be a data-format bug rather than a cosmetic one.
     */
    TableSource source = parse(
        "BEGIN_DEFINITION\nNAME = C\nEND_DEFINITION\n"
        "BEGIN_TABLE\n"
        "a\t\xE6\x88\x91\t1000\n"                  /* 我 */
        "b\t\xF0\xAA\x9B\x96\t900\n"               /* U+2A6D6, dropped */
        "c\t\xE7\x9A\x84\t800\n"                   /* 的 */
        "d\t\xE6\x88\x91\xF0\xAA\x9B\x96\t700\n"   /* 我 + U+2A6D6, dropped */
        "END_TABLE\n");

    PT_CHECK(apply_coverage_filter(&source) == 2);
    PT_CHECK(source.phrases.size() == 2);
    PT_CHECK(source.phrases[0].phrase == "\xE6\x88\x91");
    PT_CHECK(source.phrases[1].phrase == "\xE7\x9A\x84");

    /* Filtering an already-clean table changes nothing and reports nothing. */
    PT_CHECK(apply_coverage_filter(&source) == 0);
    PT_CHECK(source.phrases.size() == 2);
}

}  // namespace

int main(void)
{
    test_synthetic_table();
    test_comments_and_scim_header();
    test_nosymbol_and_extra_column();
    test_malformed_rows_are_skipped();
    test_char_prompts();
    test_goucima_derivation();
    test_rules();
    test_commit_boundaries();
    test_like_pattern();
    test_variant_classification();
    test_ranking();
    test_frequency_transfer();
    test_declared_options();
    test_coverage();
    return pt_report("core.table");
}
